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
 ** \file Fragment.hh
 **
 ** \brief Defines io structures for pre-bam bin fragment io.
 ** 
 ** \author Roman Petrovski
 **/

#ifndef iSAAC_IO_FRAGMENT_HH
#define iSAAC_IO_FRAGMENT_HH

#include "alignment/FragmentMetadata.hh"
#include "alignment/BamTemplate.hh"
#include "alignment/Cigar.hh"
#include "alignment/Quality.hh"

namespace isaac
{
namespace io
{

struct FragmentAccessor;
/**
 * \brief In terms of duplicate detection, anchor is same for duplicate candidates
 */
union FragmentIndexAnchor
{
    FragmentIndexAnchor() : value_(0UL){}
    explicit FragmentIndexAnchor(uint64_t value) : value_(value){}
    FragmentIndexAnchor(const alignment::FragmentMetadata & fragment);
    FragmentIndexAnchor(const FragmentAccessor & fragment);

    // Aligned reads are anchored to their lowest cycles. This means that f-stranded reads are
    // anchored to their f-strand position and r-stranded to their r-strand alignment position
    // store ReferencePosition::value as ReferencePosition itself cannot be stored in a union
    uint64_t pos_;

    // use bases for duplicate detection of shadow (unaligned) reads.
    // the CASAVA uses 8 bases of the shadow.
    // TODO: decide whether dupe-detection will benefit from using more than
    // 32 bases of the shadow for now
    uint64_t shadowBases_;

    uint64_t value_;
};

inline std::ostream &operator <<(std::ostream& os, const FragmentIndexAnchor& anchor)
{
    return os << "FragmentIndexAnchor(" << anchor.value_ << ")";
}


/**
 * \brief returns a value which can be used when ranking duplicates to chose the best one
 */
inline uint64_t getTemplateDuplicateRank(const alignment::BamTemplate &templ)
{
    return static_cast<uint64_t>(templ.getQuality()) << 32 |
        (templ.getTotalReadLength() - templ.getEditDistance()) << 16 |
        templ.getAlignmentScore();
}

struct FragmentHeader
{
    FragmentHeader():
        bamTlen_(0),
        fStrandPosition_(),
        fStrandOriginalPosition_(),
        rStrandPosition_(),
        lowClipped_(0),
        highClipped_(0),
        alignmentScore_(DODGY_ALIGNMENT_SCORE),
        templateAlignmentScore_(DODGY_ALIGNMENT_SCORE),
        mapQ_(alignment::UNKNOWN_MAPQ),
        mateFStrandPosition_(),
        readLength_(0),
        cigarLength_(0),
        nameLength_(0),
        gapCount_(0),
        editDistance_(0),
        flags_(false, false, false, false, false, false, false, false, false, false),
        tile_(uint64_t(0)-1),
        barcode_(0),
        barcodeSequence_(0),
        clusterId_(uint64_t(0)-1),
        clusterX_(POSITION_NOT_SET),
        clusterY_(POSITION_NOT_SET),
        duplicateClusterRank_(0),
        mateAnchor_(0),
        mateStorageBin_(0)
    {
    }

    FragmentHeader(const alignment::BamTemplate &bamTemplate,
                   const alignment::FragmentMetadata &fragment,
                   const alignment::FragmentMetadata &mate,
                   const unsigned barcodeIdx,
                   const unsigned mateStorageBin)
    :
        bamTlen_(getTlen(fragment, mate)),
        fStrandPosition_(fragment.isAligned() ?
            fragment.getFStrandReferencePosition() :
            mate.getFStrandReferencePosition()),
        fStrandOriginalPosition_(fStrandPosition_),
        rStrandPosition_(!fragment.isAligned() ?
                reference::ReferencePosition(reference::ReferencePosition::NoMatch) :
                fragment.getRStrandReferencePosition()),
        lowClipped_(fragment.lowClipped),
        highClipped_(fragment.highClipped),
        alignmentScore_(fragment.getAlignmentScore()),
        templateAlignmentScore_(
            bamTemplate.getAlignmentScore()),
        mapQ_(fragment.mapQ),
        mateFStrandPosition_(mate.isAligned() ?
            mate.getFStrandReferencePosition() :
            fragment.getFStrandReferencePosition()),
        readLength_(fragment.getReadLength()),
        cigarLength_(fragment.getCigarLength()),
        nameLength_(bamTemplate.getNameLength()),
        gapCount_(fragment.getGapCount()),
        editDistance_(fragment.getEditDistance()),
        flags_(true,
               !fragment.isAligned(),
               !mate.isAligned(),
               fragment.isReverse(),
               mate.isReverse(),
               1 == fragment.getReadIndex(),
               !fragment.getCluster().getPf(),
               bamTemplate.isProperPair(),
               fragment.splitAlignment,
               mate.splitAlignment),
        tile_(fragment.getCluster().getTile()),
        barcode_(barcodeIdx),
        barcodeSequence_(fragment.getCluster().getBarcodeSequence()),
        clusterId_(fragment.getCluster().getId()),
        clusterX_(fragment.getCluster().getXy().isSet() ? fragment.getCluster().getXy().x_ : POSITION_NOT_SET),
        clusterY_(fragment.getCluster().getXy().isSet() ? fragment.getCluster().getXy().y_ : POSITION_NOT_SET),
        duplicateClusterRank_(getTemplateDuplicateRank(bamTemplate)),
        mateAnchor_(mate),
        mateStorageBin_(mateStorageBin)
        //, magic_(magicValue_)
    {
    }

    // single-ended constructor
    FragmentHeader(const alignment::BamTemplate &bamTemplate,
                   const alignment::FragmentMetadata &fragment,
                   const unsigned barcodeIdx)
    :
        // According to SAM v1.4 TLEN is 0 for single-ended templates.
        bamTlen_(0),
        fStrandPosition_(fragment.getFStrandReferencePosition()),
        fStrandOriginalPosition_(fStrandPosition_),
        rStrandPosition_(!fragment.isAligned() ?
            reference::ReferencePosition(reference::ReferencePosition::NoMatch) :
            fragment.getRStrandReferencePosition()),
        lowClipped_(fragment.lowClipped),
        highClipped_(fragment.highClipped),
        alignmentScore_(fragment.getAlignmentScore()),
        templateAlignmentScore_(fragment.getAlignmentScore()),
        mapQ_(fragment.mapQ),
        mateFStrandPosition_(reference::ReferencePosition::NoMatch),
        readLength_(fragment.getReadLength()),
        cigarLength_(fragment.getCigarLength()),
        nameLength_(bamTemplate.getNameLength()),
        gapCount_(fragment.getGapCount()),
        editDistance_(fragment.getEditDistance()),
        flags_(false,
               !fragment.isAligned(),
               true,
               fragment.isReverse(),
               false,
               true,
               !fragment.getCluster().getPf(),
               false,
               fragment.splitAlignment,
               false),
        tile_(fragment.getCluster().getTile()),
        barcode_(barcodeIdx),
        barcodeSequence_(fragment.getCluster().getBarcodeSequence()),
        clusterId_(fragment.getCluster().getId()),
        clusterX_(fragment.getCluster().getXy().isSet() ? fragment.getCluster().getXy().x_ : POSITION_NOT_SET),
        clusterY_(fragment.getCluster().getXy().isSet() ? fragment.getCluster().getXy().y_ : POSITION_NOT_SET),
        duplicateClusterRank_(0),
        mateAnchor_(0),
        mateStorageBin_(0)
        //, magic_(magicValue_)
    {
    }

    bool operator ==(const FragmentHeader &that) const
    {
        return !std::memcmp(this, &that, sizeof(*this));
    }

    bool operator !=(const FragmentHeader &that) const
    {
        return !(*this == that);
    }

    static unsigned getDataLength(const unsigned readLength, const unsigned cigarLength) {
        return readLength + cigarLength;
    }

    static unsigned getTotalLength(const unsigned readLength, const unsigned cigarLength, const unsigned nameLength) {
        // name has terminating 0 in it
        return sizeof(FragmentHeader) + getDataLength(readLength, cigarLength) + nameLength + 1;
    }

    unsigned getTotalLength() const {
        return getTotalLength(readLength_, cigarLength_ * sizeof(alignment::Cigar::value_type), nameLength_);
    }

    static unsigned getMaxTotalLength(const unsigned readLength, const unsigned maxGapsPerRead, const unsigned nameLength)
    {
        return getTotalLength(readLength, alignment::Cigar::getMaxLength(maxGapsPerRead), nameLength);
    }

    static unsigned getMinTotalLength(const unsigned readLength, const unsigned nameLength)
    {
        return getTotalLength(readLength, alignment::Cigar::getMinLength(), nameLength);
    }

    /*
     * \brief as defined for TLEN in SAM v1.4
     */
    static int getTlen(const reference::ReferencePosition fragmentBeginPos,
                       const reference::ReferencePosition fragmentEndPos,
                       const reference::ReferencePosition mateBeginPos,
                       const reference::ReferencePosition mateEndPos,
                       const bool firstRead)
    {
        const uint64_t distance = (
            std::max(fragmentEndPos, mateEndPos).getLocation() -
            std::min(fragmentBeginPos, mateBeginPos).getLocation());


        const int64_t ret = fragmentBeginPos < mateBeginPos ? distance :
            (fragmentBeginPos > mateBeginPos || !firstRead) ? -distance : distance ;

        return ret;
    }

    /*
     * \brief as defined for TLEN in SAM v1.4. 0 for singletons and shadows
     */
    static int getTlen(const alignment::FragmentMetadata &fragment,
                       const alignment::FragmentMetadata &mate)
    {
        return !fragment.isAligned() || !mate.isAligned() ? 0:
            getTlen(fragment.getBeginReferencePosition(), fragment.getEndReferencePosition(),
                    mate.getBeginReferencePosition(), mate.getEndReferencePosition(), 0 == fragment.getReadIndex());
    }

    bool isAligned() const {return !flags_.unmapped_;}
    bool isMateAligned() const {return !flags_.mateUnmapped_;}
    bool isReverse() const {return flags_.reverse_;}

    unsigned getContigId() const {return fStrandPosition_.getContigId();}
    // As ReferencePosition uses a few bits for contig, it is completely safe to convert position into signed int64_t
    int64_t getPosition() const {return fStrandPosition_.getPosition();}
    int64_t getOriginalPosition() const {return fStrandOriginalPosition_.getPosition();}

    /**
     * \brief Normal reads  : positive distance between fStrandPosition_ and the base following
     *                        the last unclipped base of the fragment
     *        Others        : undefined
     */
    unsigned getObservedLength() const {return rStrandPosition_ - fStrandPosition_;}

    /// Position of the fragment
    const reference::ReferencePosition &getFStrandReferencePosition() const
    {
        ISAAC_ASSERT_MSG(isAligned(), "Must be aligned fragment");
        return fStrandPosition_;
    }
    /// Position of the fragment
    reference::ReferencePosition getRStrandReferencePosition() const
    {
        return rStrandPosition_;
    }
    /// Position of the fragment
    reference::ReferencePosition getStrandReferencePosition() const {
        return isReverse() ? getRStrandReferencePosition() : getFStrandReferencePosition();
    }

    /// \return number of bases clipped on the left side of the fragment in respect to the reference
    unsigned short leftClipped() const {return flags_.reverse_ ? highClipped_ : lowClipped_;}
    /// \return number of bases clipped on the right side of the fragment in respect to the reference
    unsigned short rightClipped() const {return flags_.reverse_ ? lowClipped_ : highClipped_;}

    const unsigned char *bytesBegin() const {return reinterpret_cast<const unsigned char *>(this);}
    const unsigned char *bytesEnd() const {return reinterpret_cast<const unsigned char *>(this+1);}

    bool isClusterXySet() const {return POSITION_NOT_SET != clusterX_;}

    bool isSplit() const {return flags_.splitAlignment_;}

    /**
     * \brief template length as specified by SAM format
     * TLEN: signed observed Template LENgth. If all segments are mapped to the same reference, the
     * unsigned observed template length equals the number of bases from the leftmost mapped base
     * to the rightmost mapped base. The leftmost segment has a plus sign and the rightmost has a
     * minus sign. The sign of segments in the middle is undefined. It is set as 0 for single-segment
     * template or when the information is unavailable.
     */
    int bamTlen_;


    // same as fStrandPosition_ initially, but does not get changed by gap realignment and such
    reference::ReferencePosition fStrandPosition_;
    reference::ReferencePosition fStrandOriginalPosition_;
    reference::ReferencePosition rStrandPosition_;

    /// number of bases clipped from begin and end irrespective of alignment
    unsigned short lowClipped_;
    unsigned short highClipped_;

    static const unsigned short DODGY_ALIGNMENT_SCORE = static_cast<unsigned short>(-1U);
    /**
     * \brief single fragment alignment score
     */
    unsigned short alignmentScore_;

    /**
     * \brief template alignment score:
     *          for normal pairs:     pair alignment score.
     *          for singleton/shadow: singleton alignment score
     *          for others:           0
     */
    unsigned short templateAlignmentScore_;

    unsigned char mapQ_;

    /**
     * \brief forward-strand position of mate
     */
    reference::ReferencePosition mateFStrandPosition_;

    /**
     * \brief number of nucleotides in the fragment
     */
    unsigned short readLength_;

    /**
     * \brief number of operations in the CIGAR
     */
    unsigned short cigarLength_;

    /**
     * \brief number of characters in fragment name including the terminating 0
     */
    unsigned short nameLength_;

    /**
     * \brief number insertion and deletion operations in the CIGAR
     */
    unsigned short gapCount_;
    /**
     * \brief edit distance
     */
    unsigned short editDistance_;

    struct Flags
    {
        Flags(bool paired, bool unmapped, bool mateUnmapped, bool reverse, bool mateReverse,
              bool secondRead, bool failFilter, bool properPair, bool splitAlignment, bool mateSplit):
                  initialized_(true), paired_(paired), unmapped_(unmapped), mateUnmapped_(mateUnmapped),
            reverse_(reverse), mateReverse_(mateReverse), realigned_(false), secondRead_(secondRead),
            failFilter_(failFilter), properPair_(properPair), duplicate_(false),
            splitAlignment_(splitAlignment), mateSplit_(mateSplit){}
        bool initialized_ : 1;
        bool paired_ : 1;
        bool unmapped_ : 1;
        bool mateUnmapped_ : 1;
        bool reverse_ : 1;
        bool mateReverse_ : 1;
        bool realigned_ : 1;
        bool secondRead_ : 1;
        bool failFilter_ : 1;
        bool properPair_ : 1;
        bool duplicate_ : 1;
        bool splitAlignment_ :1;
        bool mateSplit_ :1;
    } flags_;

    /**
     * \brief 0-based unique tile index
     */
    uint64_t tile_;

    /**
     * \brief 0-based unique barcode index
     * TODO, rename to barcodeIndex_
     */
    uint64_t barcode_;

    /**
     * \brief actual barcode from the data. It might not match exactly to the one from the sample sheet
     */
    uint64_t barcodeSequence_;

    /**
     * \brief 0-based cluster index in the tile
     */
    uint64_t clusterId_;

    static const int POSITION_NOT_SET = boost::integer_traits<int>::const_max;
    /**
     * \brief pixel X * 100 position of the cluster on the tile. May be negative. Magic value of POSITION_NOT_SET means unset.
     */
    int clusterX_;

    /**
     * \brief pixel Y * 100 position of the cluster on the tile. May be negative. Magic value of POSITION_NOT_SET means unset.
     */
    int clusterY_;

    uint64_t duplicateClusterRank_;

    FragmentIndexAnchor mateAnchor_;

    unsigned mateStorageBin_;
//    unsigned short magic_;
//    static const unsigned short magicValue_ = 0xb1a;

};

inline std::ostream & operator <<(std::ostream &os, const FragmentHeader::Flags &headerFlags)
{
    return os << "FragmentHeader::Flags(" <<
        (headerFlags.paired_ ? "pe" : "") << "|" <<
        (headerFlags.unmapped_ ? "u":"") << "|" <<
        (headerFlags.mateUnmapped_ ? "mu" : "") << "|" <<
        (headerFlags.reverse_ ? "r" : "") << "|" <<
        (headerFlags.mateReverse_ ? "mr" : "") << "|" <<
        (headerFlags.realigned_ ? "re" : "") << "|" <<
        (headerFlags.secondRead_ ? "r2" : "") << "|" <<
        (headerFlags.failFilter_ ? "ff" : "") << "|" <<
        (headerFlags.properPair_ ? "pp" : "") << "|" <<
        (headerFlags.splitAlignment_ ? "sa" : "") << "|" <<
        (headerFlags.mateSplit_ ? "ms" : "") << "|" <<
        ")";
}

inline std::ostream & operator <<(std::ostream &os, const FragmentHeader &header)
{
    return os << "FragmentHeader(" <<
        header.bamTlen_ << "," <<
        header.fStrandPosition_ << "," <<
        header.rStrandPosition_ << "rs," <<
        header.alignmentScore_ << "SM," <<
        header.templateAlignmentScore_ << "AS," <<
        header.mateFStrandPosition_ << "," <<
        header.readLength_ << "rl," <<
        header.cigarLength_ << "cl," <<
        header.gapCount_ << "g," <<
        header.editDistance_ << "ed," <<
        header.flags_ << "," <<
        header.tile_ << "t," <<
        header.barcode_ << "b," <<
        header.clusterId_ << "id," <<
        header.leftClipped() << "lc," <<
        header.rightClipped() << "rc," <<
        ")";
}


struct FragmentAccessor : public FragmentHeader
{
    const unsigned char *basesBegin() const {
        return reinterpret_cast<const unsigned char*>(this) + sizeof(FragmentHeader);
    }

    unsigned char *basesBegin() {
        return reinterpret_cast<unsigned char*>(this) + sizeof(FragmentHeader);
    }

    const unsigned char *unmaskedBasesBegin() const
    {
        ISAAC_ASSERT_MSG(flags_.initialized_, "Invalid method call on an uninitialized fragment");
        if (cigarLength_)
        {
            using alignment::Cigar;
            const Cigar::Component decoded = Cigar::decode(*cigarBegin());
            if (decoded.first == Cigar::SOFT_CLIP)
            {
                return basesBegin() + decoded.second;
            }
        }
        return basesBegin();
    }
    const unsigned char *basesEnd() const
    {
        ISAAC_ASSERT_MSG(flags_.initialized_, "Invalid method call on an uninitialized fragment");
        return basesBegin() + readLength_;
    }

    unsigned *cigarBegin()
    {
        ISAAC_ASSERT_MSG(flags_.initialized_, "Invalid method call on an uninitialized fragment");
        return reinterpret_cast<unsigned*>(basesBegin() + readLength_);
    }

    const unsigned *cigarBegin() const { return reinterpret_cast<const unsigned*>(basesEnd()); }
    const unsigned *cigarEnd() const { return cigarBegin() + cigarLength_; }

    bool hasName() const {return nameLength_;}
    const char *nameBegin() const { return reinterpret_cast<const char*>(cigarEnd()); }
    const char *nameEnd() const { return nameBegin() + nameLength_; }

    const char *begin() const {return reinterpret_cast<const char*>(this);}
    const char *end() const {return begin() + getTotalLength();}
protected:
    // this structure cannot be directly instantiated. Use it to cast the byte pointers to access
    // data read or mapped from a binary file
    FragmentAccessor(){}
};

inline std::ostream & operator <<(std::ostream &os, const FragmentAccessor &fragment)
{
    const FragmentHeader &header = fragment;
    os << "FragmentAccessor(" << header <<
        ", ";
    return alignment::Cigar::toStream(fragment.cigarBegin(), fragment.cigarEnd(), os) << "," <<
        common::makeFastIoString(fragment.nameBegin(), fragment.nameEnd()) << " " <<
        fragment.mateAnchor_ << ")";
}


////////////////////FragmentIndexAnchor implementation
inline FragmentIndexAnchor::FragmentIndexAnchor(const alignment::FragmentMetadata & fragment)
{
    if(fragment.isAligned()){
        pos_ = fragment.getStrandReferencePosition().getValue();
    }else{
        shadowBases_ = oligo::pack32BclBases(fragment.getBclData());
    }
}

inline FragmentIndexAnchor::FragmentIndexAnchor(const FragmentAccessor & fragment)
{
    if(fragment.isAligned()){
        pos_ = fragment.getStrandReferencePosition().getValue();
    }else{
        shadowBases_ = oligo::pack32BclBases(fragment.basesBegin());
    }
}

} // namespace io
} // namespace isaac

#endif // #ifndef iSAAC_IO_FRAGMENT_INDEX_HH
