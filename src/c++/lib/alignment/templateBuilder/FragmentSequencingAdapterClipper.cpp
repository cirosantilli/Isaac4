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
 ** \file FragmentSequencingAdapterClipper.cpp
 **
 ** \brief See FragmentSequencingAdapterClipper.hh
 ** 
 ** \author Roman Petrovski
 **/

#include <boost/foreach.hpp>
#include <boost/format.hpp>

#include "alignment/Mismatch.hh"
#include "alignment/FragmentMetadata.hh"
#include "alignment/templateBuilder/FragmentSequencingAdapterClipper.hh"
#include "common/Debug.hh"

namespace isaac
{
namespace alignment
{
namespace templateBuilder
{

/**
 * \brief Adjusts the sequence iterators to stay within the reference.
 *
 * \return new fragment.position to point at the first not clipped base.
 */
static int64_t clipReference(
    const FragmentMetadata &fragmentMetadata,
    std::vector<char>::const_iterator &sequenceBegin,
    std::vector<char>::const_iterator &sequenceEnd,
    const int64_t referenceSize)
{
    const int64_t referenceLeft = referenceSize - fragmentMetadata.position;
    if (referenceLeft < std::distance(sequenceBegin, sequenceEnd))
    {
        sequenceEnd = sequenceBegin + referenceLeft;
    }

    if (0 > fragmentMetadata.position)
    {
        sequenceBegin -= fragmentMetadata.position;
        return 0L;
    }
    return fragmentMetadata.position;
}

/**
 * \return integer value representing % of !isMatch bases on the sequence/reference overlap
 */
unsigned percentMismatches(
    const std::vector<char>::const_iterator sequenceBegin,
    const std::vector<char>::const_iterator sequenceEnd,
    const reference::Contig::const_iterator referenceBegin,
    const reference::Contig::const_iterator referenceEnd)
{
    const unsigned overlapLength = std::min(std::distance(sequenceBegin, sequenceEnd),
                                            std::distance(referenceBegin, referenceEnd));
    return countMismatches(sequenceBegin, sequenceEnd, referenceBegin, referenceEnd) * 100 / overlapLength;
}


const std::pair<std::vector<char>::const_iterator, std::vector<char>::const_iterator>
findSequencingAdapter(
    std::vector<char>::const_iterator sequenceBegin,
    std::vector<char>::const_iterator sequenceEnd,
    const reference::Contig::const_iterator referenceBegin,
    const reference::Contig::const_iterator referenceEnd,
    const SequencingAdapter &adapter)
{
    reference::Contig::const_iterator currentReference = referenceBegin;
    for (std::vector<char>::const_iterator currentBase(sequenceBegin);
        sequenceEnd != currentBase; ++currentReference, ++currentBase)
    {
        if (!isMatch(*currentBase, *currentReference))
        {
            const std::pair<std::vector<char>::const_iterator, std::vector<char>::const_iterator> adapterMatchRange =
                adapter.getMatchRange(sequenceBegin, sequenceEnd, currentBase);
            if (adapterMatchRange.first != adapterMatchRange.second)
            {
                return adapterMatchRange;
            }
        }
    }
    return std::make_pair(sequenceBegin, sequenceBegin);
}

/**
 * \brief if the adapter sequence has not been checked for the alignment strand,
 *        searches for the adapter and prevents further searches for adapter on this strand
 */
void FragmentSequencingAdapterClipper::checkInitStrand(
    const FragmentMetadata &fragmentMetadata,
    const reference::Contig &contig)
{
    const bool reverse = fragmentMetadata.reverse;

    StrandSequencingAdapterRange &strandAdapters = readAdapters_[fragmentMetadata.getReadIndex()];
    if (!strandAdapters.strandRange_[reverse].initialized_)
    {
        std::vector<char>::const_iterator &adapterRangeBegin = strandAdapters.strandRange_[reverse].adapterRangeBegin_;
        std::vector<char>::const_iterator &adapterRangeEnd = strandAdapters.strandRange_[reverse].adapterRangeEnd_;

        const Read &read = fragmentMetadata.getRead();

        const std::vector<char> &sequence = reverse ? read.getReverseSequence() : read.getForwardSequence();

        std::vector<char>::const_iterator sequenceBegin = sequence.begin();
        std::vector<char>::const_iterator sequenceEnd = sequence.end();

        int64_t newFragmentPos =
            clipReference(fragmentMetadata, sequenceBegin, sequenceEnd, contig.size());

        adapterRangeBegin = sequenceEnd;
        adapterRangeEnd = sequenceBegin;

        for (const SequencingAdapter &adapter : sequencingAdapters_)
        {
            if (adapter.isStrandCompatible(fragmentMetadata.isReverse()))
            {
                const std::pair<std::vector<char>::const_iterator, std::vector<char>::const_iterator> adapterMatchRange =
                    findSequencingAdapter(adapterRangeEnd, sequenceEnd,
                                          contig.begin() + newFragmentPos + std::distance(sequenceBegin, adapterRangeEnd),
                                          contig.end(), adapter);
                if (adapterMatchRange.first != adapterMatchRange.second)
                {
                    adapterRangeBegin = std::min(adapterMatchRange.first, adapterRangeBegin);
                    adapterRangeEnd = std::max(adapterMatchRange.second, adapterRangeEnd);
                    ISAAC_THREAD_CERR_DEV_TRACE("FragmentSequencingAdapterClipper::checkInitStrand found: " <<
                                                std::string(adapterRangeBegin, adapterRangeEnd) << " reverse: " << reverse);
                }
            }
        }
        strandAdapters.strandRange_[reverse].initialized_ = true;
        strandAdapters.strandRange_[reverse].empty_ = sequenceBegin == adapterRangeEnd;
    }
}

bool FragmentSequencingAdapterClipper::decideWhichSideToClip(
    const reference::Contig &contig,
    const int64_t contigPosition,
    const std::vector<char>::const_iterator sequenceBegin,
    const std::vector<char>::const_iterator sequenceEnd,
    const SequencingAdapterRange &adapterRange,
    bool &clipBackwards)
{
    // try preserve the longest useful sequence
    const int backwardsClipped = std::distance(sequenceBegin, adapterRange.adapterRangeBegin_);
    const int forwardsClipped = std::distance(adapterRange.adapterRangeEnd_, sequenceEnd);

    clipBackwards = backwardsClipped < forwardsClipped;
    // If we give too much freedom, returned results will provide conflicting alignment positions to
    // the alignment scoring mechanism and it will assign low alignment scores thinking the fragment aligns to a repeat
    const unsigned sequenceLength = std::distance(sequenceBegin, sequenceEnd);
    if (backwardsClipped && forwardsClipped && abs(backwardsClipped - forwardsClipped) < 9)
    {
        // avoid counting matches for alignments that partially fall outside the reference
        if (contigPosition >=0 && contig.size() >= unsigned(contigPosition + sequenceLength))
        {
            reference::Contig::const_iterator referenceBegin = contig.begin() + contigPosition;
            reference::Contig::const_iterator referenceEnd = referenceBegin + sequenceLength;
            // count matches if both sides are of close lengths
            const unsigned backwardsMatches = countMatches(
                sequenceBegin, adapterRange.adapterRangeBegin_, referenceBegin, referenceBegin + backwardsClipped);
            const unsigned forwardsMatches = countMatches(
                adapterRange.adapterRangeEnd_, sequenceEnd, referenceEnd - forwardsClipped, referenceEnd);

            ISAAC_THREAD_CERR_DEV_TRACE("backwards matches " << countMismatches(sequenceBegin, adapterRange.adapterRangeBegin_,
                                                                        referenceBegin, referenceBegin + backwardsClipped));
            ISAAC_THREAD_CERR_DEV_TRACE("forwards matches " << countMismatches(adapterRange.adapterRangeEnd_, sequenceEnd,
                                                                        referenceEnd - forwardsClipped, referenceEnd));

            // prefer clipping that leaves more and longer part unclipped
            clipBackwards = (backwardsMatches < forwardsMatches ||
                (backwardsMatches == forwardsMatches && backwardsClipped < forwardsClipped));
        }
    }
    else if (!backwardsClipped || !forwardsClipped)
    {
        // If it is clipping all the way to one of the ends,make sure the side that we clip has decent
        // amount of mismatches. Else, we're clipping something that just happens to have an adapter sequence in it...
        reference::Contig::const_iterator referenceBegin = contig.begin() + contigPosition;
        reference::Contig::const_iterator referenceEnd = referenceBegin + sequenceLength;
        if (clipBackwards && !backwardsClipped)
        {
            const unsigned basesClipped = std::distance(sequenceBegin, adapterRange.adapterRangeEnd_);
            ISAAC_THREAD_CERR_DEV_TRACE("backwards percent " << percentMismatches(sequenceBegin, adapterRange.adapterRangeEnd_,
                                                                        referenceBegin, referenceBegin + basesClipped));
            ISAAC_THREAD_CERR_DEV_TRACE("backwards count " << countMismatches(sequenceBegin, adapterRange.adapterRangeEnd_,
                                                                        referenceBegin, referenceBegin + basesClipped));

            return percentMismatches(sequenceBegin, adapterRange.adapterRangeEnd_,
                                     referenceBegin, referenceBegin + basesClipped) > TOO_GOOD_READ_MISMATCH_PERCENT;
        }
        else if (!clipBackwards && !forwardsClipped)
        {
            const unsigned basesClipped = std::distance(adapterRange.adapterRangeBegin_, sequenceEnd);
            ISAAC_THREAD_CERR_DEV_TRACE("forwards percent " << percentMismatches(adapterRange.adapterRangeBegin_, sequenceEnd,
                                                                        referenceEnd - basesClipped, referenceEnd));
            ISAAC_THREAD_CERR_DEV_TRACE("forwards count " << countMismatches(adapterRange.adapterRangeBegin_, sequenceEnd,
                                                                        referenceEnd - basesClipped, referenceEnd));

            return percentMismatches(adapterRange.adapterRangeBegin_, sequenceEnd,
                                     referenceEnd - basesClipped, referenceEnd) > TOO_GOOD_READ_MISMATCH_PERCENT;
        }
    }

    return true;
}
/**
 * \brief Clips off the adapters and part of the sequence that assumedly does not align.
 *        The decision on which part to clip is based on the length.
 *
 * \param contigPosition in/out: on return: new fragmentMetadata.position that points at the
 *                               first not clipped base
 */
void FragmentSequencingAdapterClipper::clip(
    const reference::Contig &contig,
    FragmentMetadata &fragment,
    std::vector<char>::const_iterator &sequenceBegin,
    std::vector<char>::const_iterator &sequenceEnd) const
{
    const SequencingAdapterRange &adapterRange = readAdapters_[fragment.getReadIndex()].strandRange_[fragment.reverse];
    ISAAC_ASSERT_MSG(adapterRange.initialized_, "checkInitStrand has not been called");
    if (!adapterRange.empty_)
    {
        // non-empty range means adapter sequence identified around the currentBase
        ISAAC_ASSERT_MSG(adapterRange.adapterRangeBegin_ >= sequenceBegin &&
                         adapterRange.adapterRangeBegin_ <= sequenceEnd, "adapter range begin is outside the sequence:" << fragment);
        ISAAC_ASSERT_MSG(adapterRange.adapterRangeEnd_ >= sequenceBegin &&
                         adapterRange.adapterRangeEnd_ <= sequenceEnd, "adapter range end is outside the sequence:" << fragment);

        bool clipBackwards = false;

        if (decideWhichSideToClip(contig, fragment.position, sequenceBegin, sequenceEnd, adapterRange, clipBackwards))
        {
            if (clipBackwards)
            {
                const std::size_t clipBases = std::distance(sequenceBegin, adapterRange.adapterRangeEnd_);
                ISAAC_THREAD_CERR_DEV_TRACE("FragmentSequencingAdapterClipper::clip: adapter in " <<
                                            std::string(sequenceBegin, sequenceEnd) <<
                                            ", clipping backwards " << clipBases << " bases");
                fragment.incrementClipLeft(clipBases);
                fragment.incrementAdapterClip(clipBases);
                sequenceBegin = adapterRange.adapterRangeEnd_;
            }
            else
            {
                const std::size_t clipBases = std::distance(adapterRange.adapterRangeBegin_, sequenceEnd);
                ISAAC_THREAD_CERR_DEV_TRACE("FragmentSequencingAdapterClipper::clip: adapter in " <<
                                            std::string(sequenceBegin, sequenceEnd) <<
                                            ", clipping forwards " << clipBases << " bases");
                fragment.incrementClipRight(clipBases);
                fragment.incrementAdapterClip(clipBases);
                sequenceEnd = adapterRange.adapterRangeBegin_;
            }
        }
        else
        {
            ISAAC_THREAD_CERR_DEV_TRACE("FragmentSequencingAdapterClipper::clip: adapter in " <<
                                        std::string(sequenceBegin, sequenceEnd) <<
                                        ", not clipping!");
        }
    }
}

} // namespace templateBuilder
} // namespace alignment
} // namespace isaac
