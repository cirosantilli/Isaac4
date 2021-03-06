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
 ** \file BinningFragmentStorage.hh
 **
 ** \brief Stores fragments in bin files without buffering.
 ** 
 ** \author Roman Petrovski
 **/

#ifndef iSAAC_ALIGNMENT_MATCH_SELECTOR_BINNING_FRAGMENT_STORAGE_HH
#define iSAAC_ALIGNMENT_MATCH_SELECTOR_BINNING_FRAGMENT_STORAGE_HH

#include <boost/noncopyable.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>

#include "BinIndexMap.hh"
#include "common/Threads.hpp"
#include "FragmentStorage.hh"
#include "FragmentBinner.hh"
#include "io/FileBufCache.hh"
#include "io/Fragment.hh"


namespace isaac
{
namespace alignment
{
namespace matchSelector
{

namespace bfs = boost::filesystem;

class BinningFragmentStorage: FragmentPacker, FragmentBinner, public FragmentStorage
{
public:
    BinningFragmentStorage(
        const boost::filesystem::path &tempDirectory,
        const bool keepUnaligned,
        const BinIndexMap &binIndexMap,
        const reference::SortedReferenceMetadata::Contigs& contigs,
        const flowcell::BarcodeMetadataList &barcodeMetadataList,
        const bool preAllocateBins,
        const uint64_t expectedBinSize,
        const uint64_t targetBinLength,
        const unsigned threads,
        alignment::BinMetadataList &binMetadataList);

    ~BinningFragmentStorage();

    virtual void store(
        const BamTemplate &bamTemplate,
        const unsigned barcodeIdx,
        const unsigned threadNumber);

    virtual void reset(const uint64_t clusterId, const bool paired)
    {

    }

    virtual void prepareFlush() noexcept;

    virtual void flush()
    {
    }
    virtual void resize(const uint64_t clusters)
    {
    }
    virtual void reserve(const uint64_t clusters)
    {
    }

    virtual void close()
    {
        FragmentBinner::flush(binMetadataList_);
        FragmentBinner::close();
    }

private:
    /// Maximum number of bytes a packed fragment is expected to take. Change and recompile when needed
    static const unsigned FRAGMENT_BYTES_MAX = 10*1024;
    static const unsigned READS_MAX = 2;
    const BinIndexMap &binIndexMap_;
    const uint64_t expectedBinSize_;
    alignment::BinMetadataList &binMetadataList_;
    // this is just a bunch of BinMetadata objects ready to be moved into binMetadataList_ to avoid dynamic memory allocation
    alignment::BinMetadataList unalignedBinMetadataReserve_;
};

} // namespace matchSelector
} // namespace alignment
} // namespace isaac

#endif // #ifndef iSAAC_ALIGNMENT_MATCH_SELECTOR_BINNING_FRAGMENT_STORAGE_HH
