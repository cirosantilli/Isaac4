/**
 ** Isaac Genome Alignment Software
 ** Copyright (c) 2010-2017 Illumina, Inc.
 ** All rights reserved.
 **
 ** This software is provided under the terms and conditions of the
 ** GNU GENERAL PUBLIC LICENSE Version 3
 **
 ** You should have received a copy of the GNU GENERAL PUBLIC LICENSE Version 3
 ** along with this program. If not, see
 ** <https://github.com/illumina/licenses/>.
 **
 ** \file SplitReadAligner.cpp
 **
 ** \brief See SplitReadAligner.hh
 ** 
 ** \author Roman Petrovski
 **/

#include "alignment/templateBuilder/SplitReadAligner.hh"
#include "alignment/Mismatch.hh"

namespace isaac
{
//#pragma GCC push_options
//#pragma GCC optimize ("0")

namespace alignment
{
namespace templateBuilder
{

SplitReadAligner::SplitReadAligner(
    const bool collectMismatchCycles,
    const AlignmentCfg &alignmentCfg)
    : AlignerBase(collectMismatchCycles, alignmentCfg)
{
}


struct DeletionLocation
{
    DeletionLocation(
        const unsigned leftRealignedMismatches,
        const unsigned rightRealignedMismatches,
        const unsigned deletionOffset,
        const std::vector<char>::const_iterator breakpointIterator,
        const reference::Contig::const_iterator headEndReferenceIterator,
        const reference::Contig::const_iterator tailBeginReferenceIterator) :
            leftRealignedMismatches_(leftRealignedMismatches), rightRealignedMismatches_(rightRealignedMismatches),
            deletionOffset_(deletionOffset),
            breakpointIterator_(breakpointIterator),
            headEndReferenceIterator_(headEndReferenceIterator),
            tailBeginReferenceIterator_(tailBeginReferenceIterator){}

    // there is not much reason to track mismatches separately except for making debug traces simpler.
    unsigned leftRealignedMismatches_;
    unsigned rightRealignedMismatches_;
    unsigned deletionOffset_;
    std::vector<char>::const_iterator breakpointIterator_;
    reference::Contig::const_iterator headEndReferenceIterator_;
    reference::Contig::const_iterator tailBeginReferenceIterator_;

    void moveNext()
    {
        const bool newLeftMismatch = !isMatch(*breakpointIterator_, *headEndReferenceIterator_);
        leftRealignedMismatches_ += newLeftMismatch;
        const bool disappearingRightMismatch = !isMatch(*breakpointIterator_, *tailBeginReferenceIterator_);
        rightRealignedMismatches_ -= disappearingRightMismatch;

        ++deletionOffset_;
        ++headEndReferenceIterator_;
        ++tailBeginReferenceIterator_;
        ++breakpointIterator_;
    }

    unsigned getMistmatches() const {return leftRealignedMismatches_ + rightRealignedMismatches_;}

    friend std::ostream &operator <<(std::ostream &os, const DeletionLocation &loc)
    {
        return os << "DeletionLocation(" <<
            "leftRealignedMismatches=" << loc.leftRealignedMismatches_ <<
            " rightRealignedMismatches=" << loc.rightRealignedMismatches_ <<
            " getMistmatches()=" << loc.getMistmatches() <<
            " deletionOffset=" << loc.deletionOffset_ << ")";
    }
};

/**
 * \brief Patches the front fragment with cigar that produces the lowest number of mismatches assuming there
 *        is a deletion in the read somewhere between the frontFragment first seed and back fragment first seed
 *
 * \param firstBreakpointOffset earliest possible breakpoint offset in the headAlignment from the left-most base in respect to the reference
 * \param lastBreakpointOffset last possible offset in tailAlignment from the left-most base in respect to the reference
 */
bool SplitReadAligner::alignSimpleDeletion(
    Cigar &cigarBuffer,
    FragmentMetadata &headAlignment,
    const unsigned firstBreakpointOffset,
    const FragmentMetadata &tailAlignment,
    const unsigned lastBreakpointOffset,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata) const
{
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignSimpleDeletion:\n" << headAlignment << "\n" << tailAlignment);

    const reference::Contig &headReference = contigList[headAlignment.contigId];
    const reference::Contig &tailReference = contigList[tailAlignment.contigId];

    ISAAC_ASSERT_MSG(int64_t(lastBreakpointOffset) - tailAlignment.getBeginClippedLength() + tailAlignment.getPosition() <= int64_t(tailReference.size()),
                     "lastBreakpointOffset " << lastBreakpointOffset << " is outside the reference " << tailReference.size() << " bases " << headAlignment << " " << tailAlignment);

    const std::vector<char>::const_iterator sequenceBegin = headAlignment.getStrandSequence().begin();
    const std::vector<char>::const_iterator breakpointIterator = sequenceBegin + firstBreakpointOffset;

    const unsigned tailEndOffset = tailAlignment.getBeginClippedLength() + tailAlignment.getObservedLength();
    if (tailEndOffset < firstBreakpointOffset)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " firstBreakpointOffset is clipped by tail end:" << firstBreakpointOffset);
        return false;
    }
    const unsigned tailLength = tailEndOffset - firstBreakpointOffset;
    ISAAC_ASSERT_MSG(headAlignment.getUnclippedPosition() >=0 || firstBreakpointOffset >= -headAlignment.getUnclippedPosition(),
                     "First breakpoint offset is left of the head reference " << firstBreakpointOffset << " >= " << -headAlignment.getUnclippedPosition())
    const reference::Contig::const_iterator headEndReferenceIterator = headReference.begin() + firstBreakpointOffset + headAlignment.getUnclippedPosition();

    const unsigned headLength = firstBreakpointOffset - headAlignment.getBeginClippedLength();
    ISAAC_ASSERT_MSG(tailAlignment.getUnclippedPosition() >=0 || firstBreakpointOffset >= -tailAlignment.getUnclippedPosition(),
                     "First breakpoint offset is left of the tail reference " << firstBreakpointOffset << " >= " << -tailAlignment.getUnclippedPosition())
    reference::Contig::const_iterator tailBeginReferenceIterator = tailReference.begin() + firstBreakpointOffset + tailAlignment.getUnclippedPosition();

//    ISAAC_THREAD_CERR << "headAlignment.getUnclippedPosition():" << headAlignment.getUnclippedPosition() << std::endl;
///    ISAAC_THREAD_CERR << "headAlignment.getBeginClippedLength():" << headAlignment.getBeginClippedLength() << std::endl;
//    ISAAC_THREAD_CERR << "headAlignment.getObservedLength():" << headAlignment.getObservedLength() << std::endl;
//    ISAAC_THREAD_CERR << "firstBreakpointOffset:" << firstBreakpointOffset << std::endl;
//    ISAAC_THREAD_CERR << "tailLength:" << tailLength << std::endl;
//    ISAAC_THREAD_CERR << "headLength:" << headLength << std::endl;
//    assert(0);
    // number of tail mismatches when deletion is not introduced
    const unsigned tailMismatches = countMismatches(
        breakpointIterator, headEndReferenceIterator, headReference.end(), tailLength, [](char c){return c;});
    if (!tailMismatches)
    {
        ISAAC_THREAD_CERR_DEV_TRACE("alignSimpleDeletion: no point to try, the head alignment is already good enough");
        return false;
    }

    // try to introduce the deletion on the headFragment see if the number of mismatches reduces below the original

    ISAAC_THREAD_CERR_DEV_TRACE(" alignSimpleDeletion: headLength=" << headLength << " firstBreakpointOffset=" << firstBreakpointOffset << " tailAlignment.getUnclippedPosition()=" << tailAlignment.getUnclippedPosition() <<
        " seq=" << common::makeFastIoString(breakpointIterator, headAlignment.getStrandSequence().end()) /*<<
        " ref=" << common::makeFastIoString(tailBeginReferenceIterator, reference.end())*/);
    // we're starting at the situation where the whole tail of the head alignment is moved by deletionLength
    // number of mismatches before breakpoint when deletion is at the leftmost possible position
    unsigned leftRealignedMismatches = countMismatches(
        sequenceBegin + headAlignment.getBeginClippedLength(),
        headReference.begin() + headAlignment.position, headReference.end(), headLength, [](char c){return c;});

    // number of mismatches after breakpoint when deletion is at the leftmost possible position
    unsigned rightRealignedMismatches = countMismatches(
        breakpointIterator, tailBeginReferenceIterator, tailReference.end(), tailLength, [](char c){return c;});

    ISAAC_THREAD_CERR_DEV_TRACE(" alignSimpleDeletion " <<
                                tailMismatches << "htmm " << rightRealignedMismatches << ":" << leftRealignedMismatches << "rhtrmm:lhtrmm ");

    DeletionLocation bestLocation(
        leftRealignedMismatches, rightRealignedMismatches,
        firstBreakpointOffset, breakpointIterator, headEndReferenceIterator, tailBeginReferenceIterator);

    ISAAC_ASSERT_MSG(bestLocation.rightRealignedMismatches_ != -1U, "Mismatches must not drop below 0" << headAlignment << " " << tailAlignment);

    for (DeletionLocation currentLocation = bestLocation;
        bestLocation.getMistmatches() && currentLocation.deletionOffset_ <= lastBreakpointOffset;
        currentLocation.moveNext())
    {
        ISAAC_ASSERT_MSG(currentLocation.rightRealignedMismatches_ != -1U, "Mismatches must not drop below 0" << headAlignment << " " << tailAlignment);

        if (bestLocation.getMistmatches() > currentLocation.getMistmatches())
        {
            bestLocation = currentLocation;
            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "bestLocation=" << bestLocation);
//            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(
//                headAlignment.getCluster().getId(),
//                "\n head=" << common::makeFastIoString(sequenceBegin, bestLocation.breakpointIterator_) <<
//                "\n headRef=" << common::makeFastIoString(headReference.begin() + headAlignment.getUnclippedPosition(), headReference.begin() + headAlignment.getUnclippedPosition() + headAlignment.getReadLength()));
//
//            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(
//                headAlignment.getCluster().getId(),
//                "\n tail=" << common::makeFastIoString(bestLocation.breakpointIterator_, tailAlignment.getStrandSequence().end()) <<
//                "\n tailRef=" << common::makeFastIoString(tailReference.begin() + tailAlignment.getUnclippedPosition(), tailReference.begin() + tailAlignment.getUnclippedPosition() + tailAlignment.getReadLength()));
        }

//        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "currentLocation=" << currentLocation);
    }

    const int deletionLength = boost::numeric_cast<int>(std::distance(tailReference.begin(), tailBeginReferenceIterator) - std::distance(headReference.begin(), headEndReferenceIterator));
    return mergeDeletionAlignments(
        cigarBuffer, headAlignment, tailAlignment, bestLocation.deletionOffset_, contigList,
        bestLocation.getMistmatches(), deletionLength, readMetadata);
}

bool SplitReadAligner::mergeDeletionAlignments(
    Cigar &cigarBuffer,
    FragmentMetadata &headAlignment,
    const FragmentMetadata &tailAlignment,
    const unsigned bestOffset,
    const reference::ContigList &contigList,
    const unsigned bestMismatches,
    const int deletionLength,
    const flowcell::ReadMetadata &readMetadata) const
{
    const int64_t clippingPositionOffset = headAlignment.getBeginClippedLength();
    const unsigned headMapped = bestOffset - clippingPositionOffset;

    if (headMapped != headAlignment.getObservedLength())
    {
        const unsigned cigarOffset = cigarBuffer.size();

        if (clippingPositionOffset)
        {
            cigarBuffer.addOperation(clippingPositionOffset, Cigar::SOFT_CLIP);
        }

        if (headMapped)
        {
            cigarBuffer.addOperation(headMapped, Cigar::ALIGN);
            if (tailAlignment.contigId != headAlignment.contigId)
            {
                cigarBuffer.addOperation(tailAlignment.contigId, Cigar::CONTIG);
            }
            cigarBuffer.addOperation(deletionLength, Cigar::DELETE);
        }
        else
        {
            // prevent cigars starting from deletion.
            headAlignment.contigId = tailAlignment.contigId;
            headAlignment.position += deletionLength;
        }

        const unsigned tailMapped = tailAlignment.getObservedLength()  + tailAlignment.getBeginClippedLength() - bestOffset;

//        const unsigned rightMapped = headAlignment.getObservedLength()  + headAlignment.getEndClippedLength() - leftMapped - tailAlignment.getEndClippedLength();
        if (tailMapped)
        {
            cigarBuffer.addOperation(tailMapped, Cigar::ALIGN);
        }

        const unsigned clipEndBases = tailAlignment.getEndClippedLength();
        if (clipEndBases)
        {
            cigarBuffer.addOperation(clipEndBases, Cigar::SOFT_CLIP);
        }

        headAlignment.resetAlignment();
        // carry over alignment-independent clipping information (quality trimming, adapter masking)
        headAlignment.rightClipped() = tailAlignment.rightClipped();
        ISAAC_VERIFY_MSG(updateFragmentCigar(
            readMetadata, contigList, headAlignment,
            headAlignment.reverse, headAlignment.contigId,
            headAlignment.position + clippingPositionOffset,
            cigarBuffer, cigarOffset), "The alignment can't have no matches here:" << headAlignment);

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignSimpleDeletion done: " << headAlignment << "-" << tailAlignment);
        return true;
    }
    else
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignSimpleDeletion head takes the whole length: " <<
            "headMapped:" << headMapped);
    }
    return false;
}

struct InversionLocation
{
    InversionLocation(
        const unsigned headMismatches,
        const unsigned tailMismatches,
        const unsigned breakpointOffset,
        const std::vector<char>::const_iterator headBreakpointIterator,
        const std::reverse_iterator<std::vector<char>::const_iterator> tailBreakpointIterator,
        const reference::Contig::const_iterator headEndReferenceIterator,
        const std::reverse_iterator<reference::Contig::const_iterator> tailBeginReferenceIterator) :
            headMismatches_(headMismatches), tailMismatches_(tailMismatches),
            breakpointOffset_(breakpointOffset),
            headBreakpointIterator_(headBreakpointIterator),
            tailBreakpointIterator_(tailBreakpointIterator),
            headReferenceIterator_(headEndReferenceIterator),
            tailReferenceIterator_(tailBeginReferenceIterator){}

    // there is not much reason to track mismatches separately except for making debug traces simpler.
    unsigned headMismatches_;
    unsigned tailMismatches_;
    unsigned breakpointOffset_;
    std::vector<char>::const_iterator headBreakpointIterator_;
    std::reverse_iterator<std::vector<char>::const_iterator> tailBreakpointIterator_;
    reference::Contig::const_iterator headReferenceIterator_;
    std::reverse_iterator<reference::Contig::const_iterator> tailReferenceIterator_;

    /**
     * \param headExtension 1 when moveNext extends head alignment. -1 when it makes it shorter
     */
    template <int headExtension>
    void moveNext()
    {
        ISAAC_ASSERT_MSG(tailMismatches_ != -1U, "Mismatches must not drop below 0");
        headMismatches_ += headExtension * !isMatch(*headBreakpointIterator_, *headReferenceIterator_);
        tailMismatches_ -= headExtension * !isMatch(*tailBreakpointIterator_, *tailReferenceIterator_);

        ++breakpointOffset_;
        ++headReferenceIterator_;
        ++tailReferenceIterator_;
        ++headBreakpointIterator_;
        ++tailBreakpointIterator_;
    }

    unsigned getMistmatches() const {return headMismatches_ + tailMismatches_;}

    friend std::ostream &operator <<(std::ostream &os, const InversionLocation &loc)
    {
        return os << "InversionLocation(" <<
            "headMismatches_=" << loc.headMismatches_ <<
            " tailMismatches_=" << loc.tailMismatches_ <<
            " inversionOffset=" << loc.breakpointOffset_ << ")";
    }
};

/**
 * \brief Inversion in which the left sides of the alignments are anchored
 *
 * \param headAlignment alignment that gets extended on each iteration
 */
bool SplitReadAligner::alignLeftAnchoredInversion(
    Cigar &cigarBuffer,
    FragmentMetadata &headAlignment,
    const unsigned firstBreakpointOffset,
    const FragmentMetadata &tailAlignment,
    const unsigned lastBreakpointOffset,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata) const
{
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignLeftAnchoredInversion:\n" << headAlignment << "\n" << tailAlignment);

    const reference::Contig &headReference = contigList[headAlignment.contigId];
    const reference::Contig &tailReference = contigList[tailAlignment.contigId];

    const std::vector<char>::const_iterator headSequenceBegin = headAlignment.getStrandSequence().begin();
    std::vector<char>::const_iterator headBreakpointIterator = headSequenceBegin + firstBreakpointOffset;
    const std::reverse_iterator<std::vector<char>::const_iterator> tailSequenceBegin(tailAlignment.getStrandSequence().end());
    std::reverse_iterator<std::vector<char>::const_iterator> tailBreakpointIterator = tailSequenceBegin + firstBreakpointOffset;

    const reference::Contig::const_iterator headEndReferenceIterator = headReference.begin() + headAlignment.getUnclippedPosition() + firstBreakpointOffset;
    ISAAC_ASSERT_MSG(std::size_t(tailAlignment.getUnclippedPosition() + tailAlignment.getReadLength() - firstBreakpointOffset) <= tailReference.size(),
                     "overrun:" << tailAlignment << " firstBreakpointOffset:" << firstBreakpointOffset);
    std::reverse_iterator<reference::Contig::const_iterator> tailBeginReferenceIterator(
        // explicit brackets requried to avoid debug glibc catching iterator overrun on unoptimized code.
        tailReference.begin() + (tailAlignment.getUnclippedPosition() + tailAlignment.getReadLength() - firstBreakpointOffset));

    if (firstBreakpointOffset < headAlignment.getBeginClippedLength())
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " firstBreakpointOffset:" << firstBreakpointOffset << "in head begin clipping");
        return false;
    }
    const unsigned headLength = firstBreakpointOffset - headAlignment.getBeginClippedLength();
    ISAAC_ASSERT_MSG(tailAlignment.getReadLength() - firstBreakpointOffset >= tailAlignment.getBeginClippedLength(),
                     "TODO: do something in case breakpoint is located in tail begin clipping:\n" << headAlignment << "\n" << tailAlignment << " firstBreakpointOffset:" << firstBreakpointOffset);
    const unsigned tailLength = tailAlignment.getReadLength() - firstBreakpointOffset - tailAlignment.getBeginClippedLength();
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "firstBreakpointOffset:" << firstBreakpointOffset);
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "lastBreakpointOffset:" << lastBreakpointOffset);
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "tailLength:" << tailLength);
    // number of tail mismatches when breakpoint is not introduced
    const unsigned tailMismatches = countMismatches(
        headBreakpointIterator, headEndReferenceIterator, headReference.end(), tailLength, [](char c){return c;});
    if (!tailMismatches)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "alignLeftAnchoredInversion: no point to try, the head alignment is already good enough");
        return false;
    }

    // try to introduce the deletion on the headFragment see if the number of mismatches reduces below the original

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignLeftAnchoredInversion: headLength=" << headLength << " firstBreakpointOffset=" << firstBreakpointOffset << " tailAlignment.getUnclippedPosition()=" << tailAlignment.getUnclippedPosition() <<
        " seq=" << common::makeFastIoString(headBreakpointIterator, headAlignment.getStrandSequence().end()) /*<<
        " ref=" << common::makeFastIoString(tailBeginReferenceIterator, reference.end())*/);
    // we're starting at the situation where the whole tail of the head alignment is moved by deletionLength
    // number of mismatches before breakpoint when deletion is at the leftmost possible position
    unsigned leftRealignedMismatches = countMismatches(
        headSequenceBegin + headAlignment.getBeginClippedLength(),
        headReference.begin() + headAlignment.position, headReference.end(), headLength, [](char c){return c;});

    // number of mismatches after breakpoint when deletion is at the leftmost possible position
    unsigned rightRealignedMismatches = countMismatches(
        tailBreakpointIterator, tailBeginReferenceIterator, std::reverse_iterator<reference::Contig::const_iterator>(tailReference.begin()), tailLength, [](char c){return c;});

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignLeftAnchoredInversion " <<
                                tailMismatches << "htmm " << rightRealignedMismatches << ":" << leftRealignedMismatches << "rhtrmm:lhtrmm ");

    InversionLocation bestLocation(
        leftRealignedMismatches, rightRealignedMismatches,
        firstBreakpointOffset, headBreakpointIterator, tailBreakpointIterator, headEndReferenceIterator, tailBeginReferenceIterator);

    for (InversionLocation currentLocation = bestLocation;
        bestLocation.getMistmatches() && currentLocation.breakpointOffset_ <= lastBreakpointOffset;
        currentLocation.moveNext<1>())
    {
        if (bestLocation.getMistmatches() > currentLocation.getMistmatches())
        {
            bestLocation = currentLocation;
        }

//        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "currentLocation=" << currentLocation);
//        ISAAC_THREAD_CERR_DEV_TRACE(" headTail=" << common::makeFastIoString(tailIterator, read.getStrandSequence(reverse).end()) <<
//                                    " headTailRef=" << common::makeFastIoString(referenceIterator + deletionLength, referenceIterator + deletionLength + std::distance(tailIterator, read.getStrandSequence(reverse).end())));
    }

//    ISAAC_THREAD_CERR << "std::distance(headReference.begin(), headEndReferenceIterator):" << std::distance(headReference.begin(), bestLocation.headEndReferenceIterator_) << std::endl;
    const int distance = -boost::numeric_cast<int>(std::distance(headReference.begin(), bestLocation.headReferenceIterator_) - tailAlignment.position);
    return mergeLeftAnchoredInversions(cigarBuffer, headAlignment, tailAlignment, bestLocation.breakpointOffset_,
                                    contigList, bestLocation.getMistmatches(), distance, readMetadata);
}


bool SplitReadAligner::mergeLeftAnchoredInversions(
    Cigar &cigarBuffer,
    FragmentMetadata &headAlignment,
    const FragmentMetadata &tailAlignment,
    const unsigned bestOffset,
    const reference::ContigList &contigList,
    const unsigned bestMismatches,
    const int distance,
    const flowcell::ReadMetadata &readMetadata) const
{
    const int64_t beginClippingOffset = headAlignment.getBeginClippedLength();
    const unsigned beginMapped = bestOffset - beginClippingOffset;
    if (!beginMapped)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " mergeInversionAlignments best inversion is at the start. Not accepting");
        return false;
    }

    {
        const unsigned cigarOffset = cigarBuffer.size();

        if (beginClippingOffset)
        {
            cigarBuffer.addOperation(beginClippingOffset, Cigar::SOFT_CLIP);
        }

        if (beginMapped)
        {
            cigarBuffer.addOperation(beginMapped, Cigar::ALIGN);
            cigarBuffer.addOperation(headAlignment.getReadLength() - beginMapped - beginClippingOffset, Cigar::FLIP);
            if (tailAlignment.contigId != headAlignment.contigId)
            {
                cigarBuffer.addOperation(tailAlignment.contigId, Cigar::CONTIG);
            }
            cigarBuffer.addOperation(distance, Cigar::DELETE);
        }
        else
        {
            ISAAC_ASSERT_MSG(false, "TODO: Deal with this situation. The code here is a copy from deletion processing and is invalid\n" <<
                             headAlignment << "\n" << tailAlignment);
            // prevent cigars starting from deletion.
            headAlignment.contigId = tailAlignment.contigId;
            headAlignment.position += distance;
        }

        const int64_t endClippingOffset = tailAlignment.getBeginClippedLength();
        if (endClippingOffset)
        {
            cigarBuffer.addOperation(endClippingOffset, Cigar::SOFT_CLIP);
        }

        const unsigned endMapped = tailAlignment.getObservedLength()  + tailAlignment.getEndClippedLength() - bestOffset;
        if (endMapped)
        {
            cigarBuffer.addOperation(endMapped, Cigar::ALIGN);
        }
        cigarBuffer.addOperation(tailAlignment.getReadLength() - endMapped - endClippingOffset, Cigar::HARD_CLIP);

        headAlignment.resetAlignment();
        // carry over alignment-independent clipping information (quality trimming, adapter masking)
        headAlignment.rightClipped() = tailAlignment.rightClipped();
        ISAAC_VERIFY_MSG(updateFragmentCigar(
            readMetadata, contigList, headAlignment,
            headAlignment.reverse, headAlignment.contigId, headAlignment.position + beginClippingOffset,
            cigarBuffer, cigarOffset), "The alignment can't have no matches here");

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " mergeInversionAlignments done:\n" <<
            headAlignment << "\n" << tailAlignment << "\nbestMismatches:" <<  bestMismatches);
        return true;
    }
}

struct VisualizeSplitAlignments
{
    const FragmentMetadata &headAlignment_;
    const FragmentMetadata &tailAlignment_;
    const unsigned breakpointOffset_;

    VisualizeSplitAlignments(
        const FragmentMetadata &headAlignment,
        const FragmentMetadata &tailAlignment,
        const unsigned breakpointOffset):
            headAlignment_(headAlignment), tailAlignment_(tailAlignment), breakpointOffset_(breakpointOffset)
    {}

    friend std::ostream &operator <<(std::ostream &os, const VisualizeSplitAlignments &vsa)
    {
        //const int spaces = vsa.breakpointOffset_ * 2 - vsa.headAlignment_.getReadLength();
        return os << vsa.headAlignment_ << "\n" << vsa.tailAlignment_ << "\n" /*<<
            (spaces < 0 ? std::string(-spaces, ' ') : std::string()) <<
            std::string(vsa.headAlignment_.getBeginClippedLength(), 's') <<
                std::string(vsa.breakpointOffset_ - vsa.headAlignment_.getBeginClippedLength(), vsa.headAlignment_.reverse ? '<' : '>') << "|" <<
                    std::string(vsa.headAlignment_.getEndClippedLength(), 's') <<
                        std::string(vsa.headAlignment_.getReadLength() - vsa.breakpointOffset_ - vsa.headAlignment_.getEndClippedLength(), vsa.headAlignment_.reverse ? '<' : '>') << "\n" <<
            (spaces > 0 ? std::string(spaces, ' ') : std::string()) <<
            std::string(vsa.tailAlignment_.getBeginClippedLength(), 's') <<
                std::string(vsa.tailAlignment_.getReadLength() - vsa.breakpointOffset_ - vsa.tailAlignment_.getBeginClippedLength(), vsa.tailAlignment_.reverse ? '<' : '>') << "|" +
                    std::string(vsa.tailAlignment_.getEndClippedLength(), 's') <<
                        std::string(vsa.breakpointOffset_ - vsa.tailAlignment_.getEndClippedLength(), vsa.tailAlignment_.reverse ? '<' : '>')*/;
    }
};

/**
 * \brief Inversion in which the right sides of the alignments are anchored
 *
 * \param headAlignment alignment that gets shortened on each iteration
 */
bool SplitReadAligner::alignRightAnchoredInversion(
    Cigar &cigarBuffer,
    FragmentMetadata &headAlignment,
    const unsigned firstBreakpointOffset,
    const FragmentMetadata &tailAlignment,
    const unsigned lastBreakpointOffset,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata) const
{
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignRightAnchoredInversion:\n" << headAlignment << "\n" << tailAlignment);

    const reference::Contig &headReference = contigList[headAlignment.contigId];
    const reference::Contig &tailReference = contigList[tailAlignment.contigId];

    const std::vector<char>::const_iterator headSequenceBegin = headAlignment.getStrandSequence().begin();
    std::vector<char>::const_iterator headBreakpointIterator = headSequenceBegin + firstBreakpointOffset;
    const std::reverse_iterator<std::vector<char>::const_iterator> tailSequenceBegin(tailAlignment.getStrandSequence().end());
    std::reverse_iterator<std::vector<char>::const_iterator> tailBreakpointIterator = tailSequenceBegin + firstBreakpointOffset;

    const reference::Contig::const_iterator headReferenceIterator =
        headReference.begin() + headAlignment.getUnclippedPosition() + firstBreakpointOffset;
    ISAAC_ASSERT_MSG(std::size_t(tailAlignment.getUnclippedPosition() + tailAlignment.getReadLength() - firstBreakpointOffset) <= tailReference.size(),
                     "overrun:" << tailAlignment << " firstBreakpointOffset:" << firstBreakpointOffset);
    std::reverse_iterator<reference::Contig::const_iterator> tailReferenceIterator(
        // explicit brackets requried to avoid debug glibc catching iterator overrun on unoptimized code.
        tailReference.begin() + (tailAlignment.getUnclippedPosition() + tailAlignment.getReadLength() - firstBreakpointOffset));

    if (firstBreakpointOffset > headAlignment.getBeginClippedLength() + headAlignment.getObservedLength())
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " firstBreakpointOffset:" << firstBreakpointOffset << "in head end clipping");
        return false;
    }

    const unsigned headLength = headAlignment.getObservedLength() + headAlignment.getBeginClippedLength() - firstBreakpointOffset;
//    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "firstBreakpointOffset:" << firstBreakpointOffset);
//    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "headLength:" << headLength);
//    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "lastBreakpointOffset:" << lastBreakpointOffset);
    ISAAC_ASSERT_MSG(tailAlignment.getReadLength() - firstBreakpointOffset >= tailAlignment.getBeginClippedLength(),
                     "TODO: do something in case breakpoint is located in tail begin clipping:\n" << VisualizeSplitAlignments(headAlignment, tailAlignment, firstBreakpointOffset));
    ISAAC_ASSERT_MSG(firstBreakpointOffset >= tailAlignment.getEndClippedLength(),
                     "TODO: do something in case breakpoint is located in tail end clipping." << headAlignment << " " << tailAlignment);
    const unsigned tailLength = firstBreakpointOffset - headAlignment.getBeginClippedLength();
//    ISAAC_THREAD_CERR << "tailLength:" << tailLength << std::endl;
    // number of tail mismatches when breakpoint is not introduced
    const unsigned tailMismatches = countMismatches(
        headSequenceBegin + headAlignment.getBeginClippedLength(), headReferenceIterator - tailLength,
        headReferenceIterator, tailLength, [](char c){return c;})
            // assume all soft-clipped bases mismatch as they are the ones that will get revealed by introducing the inversion
            + headAlignment.getBeginClippedLength();
//    ISAAC_THREAD_CERR << "tailMismatches:" << tailMismatches << std::endl;
    if (!tailMismatches)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "alignRightAnchoredInversion: no point to try, the head alignment is already good enough");
        return false;
    }

    // number of mismatches before breakpoint when breakpoint is at the leftmost possible position
    unsigned headRealignedMismatches = countMismatches(
        headBreakpointIterator,
        headReferenceIterator, headReference.end(), headLength, [](char c){return c;});

    // number of mismatches after breakpoint when breakpoint is at the leftmost possible position
    const unsigned realignedTailLength = firstBreakpointOffset - tailAlignment.getEndClippedLength();
    unsigned tailRealignedMismatches = countMismatches(
        tailBreakpointIterator.base(), tailReferenceIterator.base(),
        tailReference.end(), realignedTailLength, [](char c){return c;});

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " realignedTail   =" << common::makeFastIoString(tailBreakpointIterator.base(), tailBreakpointIterator.base() + realignedTailLength));
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " realignedTailRef=" << common::makeFastIoString(tailReferenceIterator.base(), tailReferenceIterator.base() + realignedTailLength));


    InversionLocation bestLocation(
        headRealignedMismatches, tailRealignedMismatches,
        firstBreakpointOffset, headBreakpointIterator, tailBreakpointIterator, headReferenceIterator, tailReferenceIterator);

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignRightAnchoredInversion " << bestLocation);

    for (InversionLocation currentLocation = bestLocation;
        bestLocation.getMistmatches() && currentLocation.breakpointOffset_ < lastBreakpointOffset;
        currentLocation.moveNext<-1>())
    {
        ISAAC_ASSERT_MSG(bestLocation.tailReferenceIterator_.base() >= tailReference.begin(), "going before start of tail reference " << VisualizeSplitAlignments(headAlignment, tailAlignment, currentLocation.breakpointOffset_))
        if (bestLocation.getMistmatches() > currentLocation.getMistmatches())
        {
            bestLocation = currentLocation;
        }

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "currentLocation=" << currentLocation);
//        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "trd=" << std::distance(tailReference.begin(), bestLocation.tailReferenceIterator_.base()));
//        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "tsd=" << std::distance(tailSequenceBegin, tailBreakpointIterator));
    }

//    ISAAC_THREAD_CERR << "std::distance(headReference.begin(), headEndReferenceIterator):" << std::distance(headReference.begin(), bestLocation.headEndReferenceIterator_) << std::endl;
    const int distance = -boost::numeric_cast<int>(
        std::distance(headReference.begin(), bestLocation.headReferenceIterator_) - headAlignment.getEndClippedLength() -
        tailAlignment.position + tailAlignment.getBeginClippedLength());
    return mergeRightAnchoredInversionAlignments(cigarBuffer, headAlignment, tailAlignment, bestLocation.breakpointOffset_,
                                    contigList, bestLocation.getMistmatches(), distance, readMetadata);
}

/**
 * \param distance genomic distance from end of head alignment to breakpoint in tail alignment
 */
bool SplitReadAligner::mergeRightAnchoredInversionAlignments(
    Cigar &cigarBuffer,
    FragmentMetadata &headAlignment,
    const FragmentMetadata &tailAlignment,
    const unsigned bestOffset,
    const reference::ContigList &contigList,
    const unsigned bestMismatches,
    const int distance,
    const flowcell::ReadMetadata &readMetadata) const
{
    const unsigned beginMapped = headAlignment.getReadLength() - bestOffset - headAlignment.getEndClippedLength();
    if (!beginMapped)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " mergeRightAnchoredInversionAlignments best inversion is at the start. Not accepting");
        return false;
    }

    {
        const unsigned cigarOffset = cigarBuffer.size();

        cigarBuffer.addOperation(bestOffset, Cigar::SOFT_CLIP);

        if (beginMapped)
        {
            cigarBuffer.addOperation(beginMapped, Cigar::ALIGN);
            if (headAlignment.getEndClippedLength())
            {
                cigarBuffer.addOperation(headAlignment.getEndClippedLength(), Cigar::SOFT_CLIP);
            }
            // no need to adjust sequence offset as the softclip will do the job
            cigarBuffer.addOperation(headAlignment.getReadLength() - bestOffset - headAlignment.getEndClippedLength() - beginMapped, Cigar::FLIP);
            if (tailAlignment.contigId != headAlignment.contigId)
            {
                cigarBuffer.addOperation(tailAlignment.contigId, Cigar::CONTIG);
            }
            cigarBuffer.addOperation(distance, Cigar::DELETE);
        }
        else
        {
            ISAAC_ASSERT_MSG(false, "TODO: Deal with this situation. The code here is a copy from deletion processing and is invalid\n" <<
                             headAlignment << "\n" << tailAlignment);
        }

        cigarBuffer.addOperation(tailAlignment.getReadLength() - bestOffset, Cigar::HARD_CLIP);

        const unsigned endMapped = bestOffset - tailAlignment.getEndClippedLength();
        if (!endMapped)
        {
            // in some clipping cases, the first possible breakpoint position gets accepted but as result the tail gets entirely clipped
            // away. Just say this does not make an alignment.
            return false;
        }
        cigarBuffer.addOperation(endMapped, Cigar::ALIGN);
        if (tailAlignment.getEndClippedLength())
        {
            cigarBuffer.addOperation(tailAlignment.getEndClippedLength(), Cigar::SOFT_CLIP);
        }

        headAlignment.resetAlignment();
        // carry over alignment-independent clipping information (quality trimming, adapter masking)
        headAlignment.rightClipped() = tailAlignment.rightClipped();
        ISAAC_VERIFY_MSG(updateFragmentCigar(
            readMetadata, contigList, headAlignment,
            headAlignment.reverse, headAlignment.contigId, headAlignment.position + bestOffset,
            cigarBuffer, cigarOffset), "The alignment can't have no matches here");

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " mergeInversionAlignments done:\n" <<
            headAlignment << "\n" << tailAlignment << "\nbestMismatches:" << bestMismatches);
        return true;
    }
}


struct InsertionLocation
{
    InsertionLocation(
        const unsigned insertionLength,
        unsigned leftRealignedMismatches,
        unsigned rightRealignedMismatches,
        unsigned insertionOffset,
        unsigned tailLength) : insertionLength_(insertionLength),
            leftRealignedMismatches_(leftRealignedMismatches), rightRealignedMismatches_(rightRealignedMismatches),
            insertionOffset_(insertionOffset), tailLength_(tailLength){}

    unsigned insertionLength_;

    unsigned leftRealignedMismatches_;
    unsigned rightRealignedMismatches_;
    unsigned insertionOffset_;
    unsigned tailLength_;


    void moveNext(
        std::vector<char>::const_iterator &tailIterator,
        reference::Contig::const_iterator &referenceIterator)
    {
        ISAAC_ASSERT_MSG(rightRealignedMismatches_ != -1U, "Mismatches must not drop below 0");

        // unlike deletions, insertions consume some bases of the sequence. Thus, we check the base that exits the insertion on the left side,
        const bool newLeftMismatch = !isMatch(*(tailIterator - insertionLength_), *referenceIterator);
        leftRealignedMismatches_ += newLeftMismatch;
        // and the one that enters it on the right side
        const bool disappearingRightMismatch = !isMatch(*tailIterator, *(referenceIterator));
        rightRealignedMismatches_ -= disappearingRightMismatch;

        ++insertionOffset_;
        --tailLength_;
        ++referenceIterator;
        ++tailIterator;
    }

    unsigned getMistmatches() const {return leftRealignedMismatches_ + rightRealignedMismatches_;}

    friend std::ostream &operator <<(std::ostream &os, const InsertionLocation &loc)
    {
        return os << "InsertionLocation(" <<
            "leftRealignedMismatches=" << loc.leftRealignedMismatches_ <<
            " rightRealignedMismatches=" << loc.rightRealignedMismatches_ <<
            " insertionOffset=" << loc.insertionOffset_ <<
            ")";
    }
};


/**
 * \brief Patches the front fragment with cigar that produces the lowest number of mismatches assuming there
 *        is an insertion in the read somewhere between the headAlignment first seed and tailAlignment first seed
 *
 * \headAlignment alignment where the leftmost part matches the reference
 * \param headSeedOffset offset of the seed for headAlignment from the left-most base in respect to the reference
 * \tailAlignment alignment where the rightmost part matches the reference
 * \param tailSeedOffset offset of the seed for tailAlignment from the left-most base in respect to the reference
 */
bool SplitReadAligner::alignSimpleInsertion(
    Cigar &cigarBuffer,
    const FragmentMetadata &headAlignment,
    const unsigned headSeedOffset,
    FragmentMetadata &tailAlignment,
    const unsigned tailSeedOffset,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata) const
{
//    ISAAC_ASSERT_MSG(headAlignment.getMismatchCount() && tailAlignment.getMismatchCount(), "No point to optimize alignments\n" << headAlignment << "\n" << tailAlignment);
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignSimpleInsertion:\n" << headAlignment << "-\n" << tailAlignment);

    // Don't allow insertion to be placed within the first seed to capture the earliest possible location as
    // insertions directly reduce number of mismatches, thus causing unfair competition against non-gapped alignment candidates
    // Start from first unclipped base otherwise placing a best insertion could be problematic
    const unsigned tailOffset = std::max(tailAlignment.getBeginClippedLength(), headSeedOffset);

    const unsigned observedEnd = tailAlignment.getBeginClippedLength() + tailAlignment.getObservedLength();
    const unsigned insertionLength = boost::numeric_cast<unsigned>(headAlignment.getUnclippedPosition() - tailAlignment.getUnclippedPosition());

    if (tailSeedOffset - headSeedOffset < insertionLength)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "alignSimpleInsertion: insertion too long (must fit between the anchoring seeds):" << insertionLength << " max: " << (tailSeedOffset - headSeedOffset));
        return false;
    }

    const Read &read = headAlignment.getRead();
    const bool reverse = headAlignment.reverse;
    const std::vector<char>::const_iterator sequenceBegin = read.getStrandSequence(reverse).begin();
    const reference::Contig &contig = contigList[headAlignment.contigId];

    std::vector<char>::const_iterator tailIterator = sequenceBegin + tailOffset + insertionLength;
    int tailLength = int(observedEnd) - tailOffset - insertionLength;

    if (0 >= tailLength)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "alignSimpleInsertion: tail length is negative possibly because of too much clipping on the left." << " tailLength:" << tailLength << " observedEnd:" << observedEnd << " tailOffset:" << tailOffset << " insertionLength:" << insertionLength << " headSeedOffset:" << headSeedOffset);
        return false;
    }

    // number of mismatches when insertion is at the left extremity
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignSimpleInsertion insertionLength:" << insertionLength << " tailLength:" << tailLength);
    const unsigned tailMismatches = countMismatches(tailIterator,
                                                    contig.begin() + headAlignment.getUnclippedPosition() + tailOffset, contig.end(),
                                                    tailLength, [](char c){return c;});

/*
    if (!tailMismatches)
    {
        ISAAC_THREAD_CERR_DEV_TRACE("alignSimpleInsertion: no point to try, the head alignment is already good enough");
        return;
    }*/


    // try to introduce the insertion see if the number of mismatches reduces below the original

    // number of mismatches when insertion is at the leftmost possible position
    unsigned rightRealignedMismatches = tailMismatches;/*countMismatches(tailIterator,
                                                        reference.begin() + headAlignment.position + tailOffset, reference.end(),
                                                        tailLength, &boost::cref<char>);*/
    // we're starting at the situation where the whole tail of the head alignment is moved by -insertionLength
    unsigned leftRealignedMismatches = 0;

    reference::Contig::const_iterator referenceIterator = contig.begin() + headAlignment.getUnclippedPosition() + tailOffset;

    InsertionLocation bestLocation(
        insertionLength,
        leftRealignedMismatches, rightRealignedMismatches,
        tailOffset, tailLength);

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "headTailOffset=" << tailOffset << " tailSeedOffset=" << tailSeedOffset << " insertionLength=" << insertionLength);
    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " reference.size(): " << contig.size());

    for (InsertionLocation currentLocation = bestLocation;
        bestLocation.getMistmatches() && currentLocation.insertionOffset_ <= tailSeedOffset - insertionLength;
        currentLocation.moveNext(tailIterator, referenceIterator))
    {
        ISAAC_ASSERT_MSG(currentLocation.rightRealignedMismatches_ != -1U, "Mismatches must not drop below 0" << headAlignment << " " << tailAlignment);

        if (bestLocation.getMistmatches() >  currentLocation.getMistmatches())
        {
            bestLocation = currentLocation;
        }

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "currentLocation=" << currentLocation);
//        ISAAC_THREAD_CERR_DEV_TRACE(" headTail=" << std::string(tailIterator, read.getStrandSequence(reverse).end()) <<
//                                    " headTailRef=" << std::string(referenceIterator, referenceIterator + std::distance(tailIterator, read.getStrandSequence(reverse).end())));

    }

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "bestLocation=" << bestLocation);
    return mergeInsertionAlignments(cigarBuffer, headAlignment, tailAlignment, bestLocation.insertionOffset_,
                                    contigList, bestLocation.getMistmatches(), insertionLength, readMetadata);
}

bool SplitReadAligner::mergeInsertionAlignments(
    Cigar &cigarBuffer,
    const FragmentMetadata &headAlignment,
    FragmentMetadata &tailAlignment,
    const unsigned bestOffset,
    const reference::ContigList &contigList,
    const unsigned bestMismatches,
    const unsigned insertionLength,
    const flowcell::ReadMetadata &readMetadata) const
{
    const int64_t clippingPositionOffset = headAlignment.getBeginClippedLength();
    const unsigned leftMapped = bestOffset - clippingPositionOffset;
    ISAAC_ASSERT_MSG(leftMapped <= headAlignment.getReadLength(), "leftMapped " << leftMapped << " is too bit for read length " << headAlignment.getReadLength());
    const unsigned leftClipped = leftMapped ? clippingPositionOffset : clippingPositionOffset + insertionLength;

// assertion check commented out to fix SAAC-731
//    ISAAC_ASSERT_MSG(leftMapped, "Simple insertions are not allowed to be placed at the very beginning of the read\n" << headAlignment << "\n" << tailAlignment);
//    const reference::Contig &contig = contigList[headAlignment.contigId];
//    const unsigned headMismatches = countMismatches(sequenceBegin + clippingPositionOffset,
//                                                    contig.begin() + headAlignment.position, contig.end(),
//                                                    leftMapped, [](char c){return c;});

//    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "headMismatches " << headMismatches);
//    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "clippingPositionOffset " << clippingPositionOffset);
//    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "leftMapped " << leftMapped);

    {
        const unsigned cigarOffset = cigarBuffer.size();

        if (leftClipped)
        {
            cigarBuffer.addOperation(leftClipped, Cigar::SOFT_CLIP);
        }

        if (leftMapped)
        {
            cigarBuffer.addOperation(leftMapped, Cigar::ALIGN);
            cigarBuffer.addOperation(insertionLength, Cigar::INSERT);
        }

        const unsigned rightMapped = headAlignment.getObservedLength()  + headAlignment.getEndClippedLength() - leftMapped - tailAlignment.getEndClippedLength() - insertionLength;
        if (!rightMapped)
        {
            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), "Simple insertions are not allowed to be placed at the very end of the read " <<
                                                   headAlignment << "-" << tailAlignment << " leftMapped:" << leftMapped << " insertionLength:" << insertionLength);
            return false;
        }

        cigarBuffer.addOperation(rightMapped, Cigar::ALIGN);

        const unsigned clipEndBases = tailAlignment.getEndClippedLength();
        if (clipEndBases)
        {
            cigarBuffer.addOperation(clipEndBases, Cigar::SOFT_CLIP);
        }

        // Unlike alignSimpleDeletion, update tailAlignment as it is ordered earlier than headAlignment in the fragment list
        tailAlignment.resetAlignment();
        // carry over alignment-independent clipping information (quality trimming, adapter masking)
        tailAlignment.leftClipped() = headAlignment.leftClipped();
        ISAAC_VERIFY_MSG(updateFragmentCigar(
            readMetadata, contigList, tailAlignment,
            headAlignment.reverse, headAlignment.contigId, headAlignment.position,
            cigarBuffer, cigarOffset), "The alignment can't have no matches here" << headAlignment << "-" << tailAlignment);

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(headAlignment.getCluster().getId(), " alignSimpleInsertion done: " << headAlignment << "-" << tailAlignment);
        return true;
    }
}

bool SplitReadAligner::pickBestSplit(const bool tmp1Worked, const FragmentMetadata& tmp1, const bool tmp2Worked,
                                     const FragmentMetadata& tmp2, FragmentMetadataList& fragmentList) const
{
    if (tmp1Worked || tmp2Worked)
    {
        const FragmentMetadata& best = tmp1Worked ? (tmp2Worked ? (tmp1.isBetterGapped(tmp2) ? tmp1 : tmp2) : tmp1) : tmp2;
        ISAAC_ASSERT_MSG(
            fragmentList.capacity() > fragmentList.size(),
            "Not enough capacity to split alignments. capactiy():" << fragmentList.capacity() << " needed:" << fragmentList.size() + 1 << " " << best);
        if (!best.getGapCount())
        {
            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(tmp1.getCluster().getId(), " pickBestSplit: best alignment has no gaps " << best);
            return false;
        }
        ISAAC_ASSERT_MSG(best.getGapCount(), "UngappedAlignment in pickBestSplit: " << best);
        fragmentList.push_back(best);
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(tmp1.getCluster().getId(), " pickBestSplit: " << best);
        return true;
    }

    ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(tmp1.getCluster().getId(), " pickBestSplit: tmp1Worked || tmp2Worked " << tmp1Worked << "||" << tmp2Worked);

    return false;
}

bool SplitReadAligner::resolveConflict(
    const reference::ContigList& contigList,
    const flowcell::ReadMetadata& readMetadata,
    const bool regularIndelsOnly,
    Cigar& cigarBuffer,
    FragmentMetadataList& fragmentList,
    const FragmentMetadata &head,
    const FragmentMetadata &tail) const
{
    bool ret = false;
    const std::size_t before = cigarBuffer.size();
    // For splitting the read both ends either have to be k-unique or the split must not result in an end that leads to an anomalous template
    //            if (!(head->isKUnique() && tail->isKUnique()) &&
    //                !(templateLengthStatistics.isStable() && (head->isWellAnchored() || tail->isWellAnchored()) &&
    //                    head->reverse == tail->reverse && head->contigId == tail->contigId &&
    //                    templateLengthStatistics.getLength(*head, *tail) <= templateLengthStatistics.getMax()))
    //            {
    //                ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(
    //                    tail->getCluster().getId(),  "SplitReadAligner::alignSimpleSv combination is not k-unique "
    //                        "and will not result in proper pair template: " << *head << "-" << *tail);
    //                return;
    //            }
    if (head.reverse == tail.reverse)
    {
        if (head.firstAnchor_.second <= tail.lastAnchor_.first || tail.firstAnchor_.second <= head.lastAnchor_.first)
        {
            if (head.contigId == tail.contigId)
            {

                FragmentMetadata tmp1 = head;
                const bool tmp1Worked = alignIndel(cigarBuffer, contigList, readMetadata, regularIndelsOnly, tmp1, tail);
                FragmentMetadata tmp2 = tail;
                const bool tmp2Worked = alignIndel(cigarBuffer, contigList, readMetadata, regularIndelsOnly, tmp2, head);
                ret = pickBestSplit(tmp1Worked, tmp1, tmp2Worked, tmp2, fragmentList);
            }
            else if (!regularIndelsOnly)
            {
                FragmentMetadata tmp1 = head;
                const bool tmp1Worked = alignTranslocation(cigarBuffer, contigList, readMetadata, tmp1, tail);
                FragmentMetadata tmp2 = tail;
                const bool tmp2Worked = alignTranslocation(cigarBuffer, contigList, readMetadata, tmp2, head);
                ret = pickBestSplit(tmp1Worked, tmp1, tmp2Worked, tmp2, fragmentList);
            }
        }
        else
        {
            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(
                head.getCluster().getId(),
                "SplitReadAligner::alignSimpleSv anchor overlap: " "head.firstAnchor_.second <= tail.lastAnchor_.first || tail.firstAnchor_.second <= head.lastAnchor_.first " << head.firstAnchor_.second << "<=" << tail.lastAnchor_.first << "||" << tail.firstAnchor_.second << "<=" << head.lastAnchor_.first);
        }
        // else the head and tail anchors overlap, no point to try splitting the read between the two alignments
    }
    else if (!regularIndelsOnly)
    {
        // Both part must produce >=0 bases of sequence overlap in order for the breakpoint to be possible to
        // introduce. Otherwise we're talking about a combination of insertion and inversion which
        // isn't supported.
        if (head.getObservedLength() + tail.getObservedLength() > head.getReadLength())
        {
            FragmentMetadata tmp1 = head;
            const bool tmp1Worked = !tmp1.firstAnchor_.empty() && !tail.firstAnchor_.empty() &&
                alignLeftAnchoredInversion(
                    cigarBuffer,
                    tmp1,
                    std::max<unsigned>(tmp1.firstAnchor_.second, tail.getEndClippedLength()),
                    tail,
                    std::min<unsigned>(head.getReadLength() - tail.firstAnchor_.second,
                                       head.getReadLength() - head.getEndClippedLength()),
                    contigList, readMetadata);

            FragmentMetadata tmp2 = head;
            const bool tmp2Worked = !tmp2.lastAnchor_.empty() && !tail.lastAnchor_.empty() &&
                alignRightAnchoredInversion(
                    cigarBuffer,
                    tmp2,
                    std::max(tmp2.getReadLength() - tail.lastAnchor_.first, tmp2.getBeginClippedLength()),
                    tail,
                    std::min<unsigned>(head.lastAnchor_.first,
                                       head.getReadLength() - tail.getBeginClippedLength()),
                    contigList, readMetadata);

            ret = pickBestSplit(tmp1Worked, tmp1, tmp2Worked, tmp2, fragmentList);

        }
    }

    if (!ret)
    {
        cigarBuffer.resize(before);
    }
    return ret;
}

/**
 * \brief Catches the cases of single indel in the fragment by analyzing the
 *        seed alignment conflicts.
 *
 * \precondition The fragmentList contains unique alignments only
 */
void SplitReadAligner::alignSimpleSv(
    Cigar &cigarBuffer,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata,
    const TemplateLengthStatistics &templateLengthStatistics,
    FragmentMetadataList &fragmentList) const
{
    if (fragmentList.size() < 2)
    {
        return;
    }
    // Notice: the fragmentList.end() changes as we insert new alignments.
    // This is why we reserve capacity to ensure that reallocation does not occur as new items get pushed
    const std::size_t endOffset = fragmentList.size();
    for (std::size_t headOffset = 0; endOffset != headOffset; ++headOffset)
    {
        const FragmentMetadataList::const_iterator head = fragmentList.begin() + headOffset;
        if (!head->getMismatchCount())
        {
            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head->getCluster().getId(), "SplitReadAligner::alignSimpleSv head is good enough. skipping: " << *head )
            continue;
        }
//        if (!head->startAnchored())
//        {
//            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head->getCluster().getId(), "SplitReadAligner::alignSimpleSv head not well anchored: " << *head <<
//                                                   std::distance(fragmentList.begin(), head))
//            continue;
//        }
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head->getCluster().getId(), "SplitReadAligner::alignSimpleSv head: " << *head << headOffset)
        for (std::size_t tailOffset = headOffset + 1; endOffset != tailOffset; ++tailOffset)
        {
            FragmentMetadataList::const_iterator tail = fragmentList.begin() +  tailOffset;
            if (!tail->getMismatchCount())
            {
                ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head->getCluster().getId(), "SplitReadAligner::alignSimpleSv tail is good enough. skipping: " << *tail )
                continue;
            }

            ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(tail->getCluster().getId(), "SplitReadAligner::alignSimpleSv tail: " << *tail << tailOffset)

            // For splitting the read both ends either have to be k-unique or the split must not result in an end that leads to an anomalous template
//            if (!(head->isKUnique() && tail->isKUnique()) &&
//                !(templateLengthStatistics.isStable() && (head->isWellAnchored() || tail->isWellAnchored()) &&
//                    head->reverse == tail->reverse && head->contigId == tail->contigId &&
//                    templateLengthStatistics.getLength(*head, *tail) <= templateLengthStatistics.getMax()))
//            {
//                ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(
//                    tail->getCluster().getId(),  "SplitReadAligner::alignSimpleSv combination is not k-unique "
//                        "and will not result in proper pair template: " << *head << "-" << *tail);
//                continue;
//            }
            resolveConflict(contigList, readMetadata, false, cigarBuffer, fragmentList, *head, *tail);
        }
    }
}

bool SplitReadAligner::alignTranslocation(
    Cigar &cigarBuffer,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata,
    FragmentMetadata &head,
    const FragmentMetadata &tail) const
{
    return alignSimpleDeletion(
        cigarBuffer, head, std::max<unsigned>(tail.getBeginClippedLength(), head.firstAnchor_.second),
        tail, tail.lastAnchor_.first, contigList, readMetadata);
}

/**
 * \brief Finds optimum locations for insertion, deletion or a negative deletion given two alignments for the same read.
 *
 * \precondition head is anchored by the stretch of bases that ends at offset lower than the offset where stretch of bases anchoring the tail begins
 * \precondition The fragmentList contains unique alignments only
 */
bool SplitReadAligner::alignIndel(
    Cigar &cigarBuffer,
    const reference::ContigList &contigList,
    const flowcell::ReadMetadata &readMetadata,
    const bool regularIndelsOnly,
    FragmentMetadata &head,
    const FragmentMetadata &tail) const
{
    if (tail.lastAnchor_.empty() || head.firstAnchor_.empty())
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head.getCluster().getId(), "Tail or head anchor missing\n" << head << "\n" << tail);
        return false;
    }
    const int64_t expectedSeedDistance = tail.lastAnchor_.first - head.firstAnchor_.second;
    if(expectedSeedDistance < 0)
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head.getCluster().getId(), "Tail and head anchors overlap\n" << head << "\n" << tail);
        return false;
    }

    const int64_t distance = tail.getUnclippedPosition() - head.getUnclippedPosition();
    ISAAC_ASSERT_MSG(distance, "distance must be non-zero for gap introduction tail:" << tail << " head:" << head);
    if (std::abs(distance) < alignmentCfg_.splitGapLength_ || !regularIndelsOnly)
    {
        const int64_t headSeedPosition = head.getUnclippedPosition() + head.firstAnchor_.second;
        const int64_t tailSeedPosition = tail.getUnclippedPosition() + tail.lastAnchor_.first;
        const int64_t actualSeedDistance = tailSeedPosition - headSeedPosition;

        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head.getCluster().getId(), " alignSimpleIndels " << head << " " <<
            tail << " expectedSeedDistance:" << expectedSeedDistance << " actualSeedDistance:" << actualSeedDistance);

        if (expectedSeedDistance < actualSeedDistance)
        {
            // this handles regular deletions
            return alignSimpleDeletion(cigarBuffer, head,
                 //std::max<unsigned>(head.firstAnchor_.second, tail.getBeginClippedLength()),
                std::max(head.getBeginClippedLength(), tail.getBeginClippedLength()),
                tail,
                // empty anchor, though legal, must allow for one base at the other side of deletion
                tail.lastAnchor_.first - tail.lastAnchor_.empty(), contigList, readMetadata);
        }
        else if (0 <= actualSeedDistance || -actualSeedDistance < head.firstAnchor_.length())
        {
            // insertions are allowed only if the tail anchor is not aligned before head anchor
            FragmentMetadata tmp = tail;
            if (alignSimpleInsertion(
                cigarBuffer,
                head, head.firstAnchor_.first,
                tmp, tmp.lastAnchor_.first,
                contigList, readMetadata))
            {
                head = tmp;
                return true;
            }
        }
        else if (!regularIndelsOnly)
        {
            // this has to be the local translocation
            ISAAC_ASSERT_MSG(0 > actualSeedDistance && -actualSeedDistance >= head.firstAnchor_.length(),
                             "Unexpected combination of alignments:\n" << head << "\n" << tail);
            return alignSimpleDeletion(cigarBuffer, head,
                 //std::max<unsigned>(head.firstAnchor_.second, tail.getBeginClippedLength()),
                std::max(head.getBeginClippedLength(), tail.getBeginClippedLength()),
                tail,
                tail.lastAnchor_.first, contigList, readMetadata);
        }

    }
    else
    {
        ISAAC_THREAD_CERR_DEV_TRACE_CLUSTER_ID(head.getCluster().getId(), " alignSimpleIndels " << "tailSeed too far apart: " << distance);
    }
    return false;
    //#pragma GCC pop_options
}

} // namespace templateBuilder
} // namespace alignment
} // namespace isaac
