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
 ** \file AlignerBase.cpp
 **
 ** \brief See AlignerBase.hh
 ** 
 ** \author Roman Petrovski
 **/
#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>

#include "alignment/templateBuilder/AlignerBase.hh"
#include "alignment/Mismatch.hh"

//#pragma GCC push_options
//#pragma GCC optimize ("0")


namespace isaac
{
namespace alignment
{
namespace templateBuilder
{

AlignerBase::AlignerBase(
    const bool collectMismatchCycles,
    const AlignmentCfg &alignmentCfg)
    : collectMismatchCycles_(collectMismatchCycles),
      alignmentCfg_(alignmentCfg)
{
}

/**
 * \brief Adjusts the sequence iterators to stay within the reference. Adjusts sequenceBeginReferencePosition
 *        to point at the first not clipped base.
 *
 */
void AlignerBase::clipReference(
    const int64_t referenceSize,
    int64_t &fragmentPosition,
    std::vector<char>::const_iterator &sequenceBegin,
    std::vector<char>::const_iterator &sequenceEnd)
{
    const int64_t referenceLeft = referenceSize - fragmentPosition;
    if (referenceLeft >= 0)
    {
        if (referenceLeft < std::distance(sequenceBegin, sequenceEnd))
        {
            sequenceEnd = sequenceBegin + referenceLeft;
        }

        if (0 > fragmentPosition)
        {
            sequenceBegin -= fragmentPosition;
            fragmentPosition = 0L;
        }

        // in some cases other clipping can end the sequence before the reference even begins
        // or begin after it ends...
        sequenceEnd = std::max(sequenceEnd, sequenceBegin);
    }
    else
    {
        // the picard sam ValidateSamFile does not like it when alignment position points to the next base after the end of the contig.
        fragmentPosition += referenceLeft - 1;
        sequenceBegin += referenceLeft - 1;
        --sequenceBegin;
        sequenceEnd = sequenceBegin;
    }
}

/**
 * \brief Sets the sequence iterators according to the masking information stored in the read.
 *        Adjusts fragment.position to point at the first non-clipped base.
 *
 */
void AlignerBase::clipReadMasking(
    const alignment::Read &read,
    FragmentMetadata &fragment,
    std::vector<char>::const_iterator &sequenceBegin,
    std::vector<char>::const_iterator &sequenceEnd)
{
    std::vector<char>::const_iterator maskedBegin;
    std::vector<char>::const_iterator maskedEnd;
    if (fragment.reverse)
    {
        maskedBegin = read.getReverseSequence().begin() + read.getEndCyclesMasked();
        maskedEnd = read.getReverseSequence().end() - read.getBeginCyclesMasked();
    }
    else
    {
        maskedBegin = read.getForwardSequence().begin() + read.getBeginCyclesMasked();
        maskedEnd = read.getForwardSequence().end() - read.getEndCyclesMasked();
    }

    if (maskedBegin > sequenceBegin)
    {
        fragment.incrementClipLeft(std::distance(sequenceBegin, maskedBegin));
        sequenceBegin = maskedBegin;
    }

    if (maskedEnd < sequenceEnd)
    {
        fragment.incrementClipRight(std::distance(maskedEnd, sequenceEnd));
        sequenceEnd = maskedEnd;
    }
}

unsigned AlignerBase::updateFragmentCigar(
    const flowcell::ReadMetadata &readMetadata,
    const reference::ContigList &contigList,
    FragmentMetadata &fragmentMetadata,
    const bool reverse,
    const unsigned contigId,
    const int64_t strandPosition,
    const Cigar &cigarBuffer,
    const unsigned cigarOffset) const
{
    return fragmentMetadata.updateAlignment(
        collectMismatchCycles_,
        alignmentCfg_,
        readMetadata,
        contigList,
        reverse, contigId, strandPosition,
        cigarBuffer, cigarOffset);
}

} // namespace templateBuilder
} // namespace alignment
} // namespace isaac

//#pragma GCC pop_options

