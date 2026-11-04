#include "analysisreport.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace BodyNif
{
    namespace
    {
        // ---- numbers and strings -------------------------------------------------------------------------------

        // A locale-independent number: "null" for NaN and infinity, an integer when the value is one, else the
        // shortest form of at most 6 significant digits.
        std::string number(double value)
        {
            if (!std::isfinite(value))
                return "null";
            if (value == 0.0)
                return "0"; // also for -0
            if (value == std::floor(value) && std::abs(value) < 1e15)
            {
                char buffer[32];
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
                return std::string(buffer, result.ptr);
            }
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 6);
            return std::string(buffer, result.ptr);
        }

        std::string count(std::size_t value)
        {
            return std::to_string(value);
        }

        std::string quoted(const std::string& text)
        {
            std::string out = "\"";
            for (const char c : text)
            {
                const auto byte = static_cast<unsigned char>(c);
                switch (c)
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        if (byte < 0x20)
                        {
                            static constexpr char digits[] = "0123456789abcdef";
                            out += "\\u00";
                            out += digits[byte >> 4];
                            out += digits[byte & 0xF];
                        }
                        else
                        {
                            out += c;
                        }
                        break;
                }
            }
            out += '"';
            return out;
        }

        // ---- a small deterministic JSON writer -----------------------------------------------------------------

        // Objects and arrays have one entry per line, two spaces of indent; arrays of plain numbers or short strings
        // may be written on one line with inlineArray.
        class Json
        {
        public:
            void beginObject() { open('{'); }
            void endObject() { close('}'); }
            void beginArray() { open('['); }
            void endArray() { close(']'); }

            void key(const std::string& name)
            {
                separate();
                mOut += quoted(name);
                mOut += ": ";
                mPendingValue = true;
            }

            void value(const std::string& text)
            {
                separate();
                mOut += quoted(text);
            }
            void value(const char* text) { value(std::string(text)); }
            void value(bool flag)
            {
                separate();
                mOut += flag ? "true" : "false";
            }
            void value(double v)
            {
                separate();
                mOut += number(v);
            }
            void value(std::size_t v)
            {
                separate();
                mOut += count(v);
            }

            template <class T>
            void field(const std::string& name, const T& v)
            {
                key(name);
                value(v);
            }

            void inlineArray(const std::vector<double>& values)
            {
                separate();
                mOut += '[';
                for (std::size_t i = 0; i < values.size(); ++i)
                {
                    if (i > 0)
                        mOut += ", ";
                    mOut += number(values[i]);
                }
                mOut += ']';
            }

            void stringArray(const std::vector<std::string>& values)
            {
                beginArray();
                for (const std::string& v : values)
                    value(v);
                endArray();
            }

            std::string finish()
            {
                mOut += '\n';
                return std::move(mOut);
            }

        private:
            void open(char c)
            {
                separate();
                mOut += c;
                mFirst.push_back(true);
            }

            void close(char c)
            {
                const bool empty = mFirst.back();
                mFirst.pop_back();
                if (!empty)
                {
                    mOut += '\n';
                    mOut.append(mFirst.size() * 2, ' ');
                }
                mOut += c;
            }

            // Before any value or key: a comma after the previous entry, then a new line and the indent. A value that
            // follows its key is on the same line.
            void separate()
            {
                if (mPendingValue)
                {
                    mPendingValue = false;
                    return;
                }
                if (mFirst.empty())
                    return;
                if (!mFirst.back())
                    mOut += ',';
                mFirst.back() = false;
                mOut += '\n';
                mOut.append(mFirst.size() * 2, ' ');
            }

            std::string mOut;
            std::vector<bool> mFirst; // per open container: nothing written in it yet
            bool mPendingValue = false;
        };

        void writeDistribution(Json& json, const std::string& name, const Distribution& d)
        {
            json.key(name);
            json.beginObject();
            json.field("count", d.mCount);
            json.field("min", d.mMin);
            json.field("median", d.mMedian);
            json.field("p95", d.mP95);
            json.field("max", d.mMax);
            json.endObject();
        }

        void writeSeamFields(Json& json, const SeamReport& r)
        {
            json.field("touches", r.mTouches);
            json.field("boundaryA", r.mBoundaryA);
            json.field("boundaryB", r.mBoundaryB);
            json.field("candidatesA", r.mCandidatesA);
            json.field("candidatesB", r.mCandidatesB);
            writeDistribution(json, "distance", r.mDistance);
            json.key("withinLadder");
            json.inlineArray({ static_cast<double>(r.mWithin[0]), static_cast<double>(r.mWithin[1]),
                static_cast<double>(r.mWithin[2]), static_cast<double>(r.mWithin[3]), static_cast<double>(r.mWithin[4]),
                static_cast<double>(r.mWithin[5]) });
            writeDistribution(json, "signedGap", r.mSignedGap);
            json.field("overlapPairs", r.mOverlapPairs);
            json.field("gapPairs", r.mGapPairs);
            json.field("meetingPairs", r.mMeetingPairs);
            writeDistribution(json, "weightDisagreement", r.mWeightDisagreement);
            json.field("boneSetDiffers", r.mBoneSetDiffers);
            json.field("uvDiffers", r.mUvDiffers);
            writeDistribution(json, "normalAngleDegrees", r.mNormalAngle);
            json.field("anyCandidatesA", r.mAnyCandidatesA);
            json.field("anyCandidatesB", r.mAnyCandidatesB);
            writeDistribution(json, "anyDistance", r.mAnyDistance);
            json.key("anyWithinLadder");
            json.inlineArray({ static_cast<double>(r.mAnyWithin[0]), static_cast<double>(r.mAnyWithin[1]),
                static_cast<double>(r.mAnyWithin[2]), static_cast<double>(r.mAnyWithin[3]),
                static_cast<double>(r.mAnyWithin[4]), static_cast<double>(r.mAnyWithin[5]) });
        }

        void writeStats(Json& json, const PartStats& s)
        {
            json.field("vertices", s.mVertices);
            json.field("triangles", s.mTriangles);
            json.field("bones", s.mBones);
            json.field("shapes", s.mShapes);
            json.field("skinned", s.mSkinned);
            json.field("boundaryVertices", s.mBoundaryVertices);
            json.key("influenceHistogram");
            json.inlineArray({ static_cast<double>(s.mInfluenceHistogram[0]),
                static_cast<double>(s.mInfluenceHistogram[1]), static_cast<double>(s.mInfluenceHistogram[2]),
                static_cast<double>(s.mInfluenceHistogram[3]), static_cast<double>(s.mInfluenceHistogram[4]) });
            json.field("overCut", s.mOverCut);
            writeDistribution(json, "weightSum", s.mWeightSum);
            json.field("weightSumOff", s.mWeightSumOff);
            json.field("duplicatePositions", s.mDuplicatePositions);
            json.field("duplicateSplits", s.mDuplicateSplits);
            json.field("openEdges", s.mOpenEdges);
            json.field("sharedEdges", s.mSharedEdges);
            json.field("overUsedEdges", s.mOverUsedEdges);
            json.key("boundsMin");
            json.inlineArray({ s.mBoundsMin[0], s.mBoundsMin[1], s.mBoundsMin[2] });
            json.key("boundsMax");
            json.inlineArray({ s.mBoundsMax[0], s.mBoundsMax[1], s.mBoundsMax[2] });
        }

        void writeAggregate(Json& json, const std::vector<SeamAggregate>& rows)
        {
            json.beginArray();
            for (const SeamAggregate& row : rows)
            {
                json.beginObject();
                json.field("label", row.mLabel);
                json.field("seams", row.mSeams);
                json.field("touching", row.mTouching);
                json.field("pairs", row.mPairs);
                json.key("withinLadder");
                json.inlineArray({ static_cast<double>(row.mWithin[0]), static_cast<double>(row.mWithin[1]),
                    static_cast<double>(row.mWithin[2]), static_cast<double>(row.mWithin[3]),
                    static_cast<double>(row.mWithin[4]), static_cast<double>(row.mWithin[5]) });
                json.field("minDistance", row.mMinDistance);
                json.field("medianDistance", row.mMedianDistance);
                json.field("maxDistance", row.mMaxDistance);
                json.field("minGap", row.mMinGap);
                json.field("medianGap", row.mMedianGap);
                json.field("maxGap", row.mMaxGap);
                json.field("overlapPairs", row.mOverlapPairs);
                json.field("gapPairs", row.mGapPairs);
                json.field("meetingPairs", row.mMeetingPairs);
                json.field("maxWeightDisagreement", row.mMaxWeightDisagreement);
                json.field("boneSetDiffers", row.mBoneSetDiffers);
                json.field("uvDiffers", row.mUvDiffers);
                json.field("maxNormalAngle", row.mMaxNormalAngle);
                json.field("anyTouching", row.mAnyTouching);
                json.field("anyPairs", row.mAnyPairs);
                json.key("anyWithinLadder");
                json.inlineArray({ static_cast<double>(row.mAnyWithin[0]), static_cast<double>(row.mAnyWithin[1]),
                    static_cast<double>(row.mAnyWithin[2]), static_cast<double>(row.mAnyWithin[3]),
                    static_cast<double>(row.mAnyWithin[4]), static_cast<double>(row.mAnyWithin[5]) });
                json.field("minAnyDistance", row.mMinAnyDistance);
                json.field("medianAnyDistance", row.mMedianAnyDistance);
                json.endObject();
            }
            json.endArray();
        }

        // ---- text ----------------------------------------------------------------------------------------------

        std::string padded(const std::string& text, std::size_t width)
        {
            std::string out = text;
            if (out.size() < width)
                out.append(width - out.size(), ' ');
            return out;
        }

        std::string distributionText(const Distribution& d)
        {
            if (d.mCount == 0)
                return "none";
            return number(d.mMin) + " / " + number(d.mMedian) + " / " + number(d.mP95) + " / " + number(d.mMax);
        }

        std::string ladderText(const std::array<std::size_t, 6>& within)
        {
            std::string out = "[";
            for (std::size_t i = 0; i < within.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                out += count(within[i]);
            }
            return out + "]";
        }

        void seamText(std::string& out, const std::string& label, const SeamReport& r)
        {
            out += "  seam " + padded(label, 16);
            std::string any;
            if (r.mAnyDistance.mCount > 0)
            {
                any = "    all vertices: pairs " + count(r.mAnyCandidatesA) + "+" + count(r.mAnyCandidatesB)
                    + ", distance min/median/p95/max " + distributionText(r.mAnyDistance) + ", within "
                    + ladderText(r.mAnyWithin) + "\n";
            }
            if (!r.mTouches)
            {
                out += " boundary " + count(r.mBoundaryA) + "/" + count(r.mBoundaryB)
                    + ": no boundary partner within the window\n" + any;
                return;
            }
            out += " boundary " + count(r.mBoundaryA) + "/" + count(r.mBoundaryB) + ", pairs " + count(r.mCandidatesA)
                + "+" + count(r.mCandidatesB) + "\n";
            out += "    distance min/median/p95/max " + distributionText(r.mDistance) + "; within (0, 1e-4, 1e-3, 1e-2, "
                "0.1, 0.5) " + ladderText(r.mWithin) + "\n";
            out += "    signed gap min/median/p95/max " + distributionText(r.mSignedGap)
                + "; overlap/meeting/gap pairs " + count(r.mOverlapPairs) + "/" + count(r.mMeetingPairs) + "/"
                + count(r.mGapPairs) + "\n";
            out += "    weight disagreement " + distributionText(r.mWeightDisagreement) + "; bone sets differ "
                + count(r.mBoneSetDiffers) + "; uv differs " + count(r.mUvDiffers) + "; normal angle "
                + distributionText(r.mNormalAngle) + "\n" + any;
        }

        void aggregateText(std::string& out, const std::string& title, const std::vector<SeamAggregate>& rows)
        {
            out += "\n" + title + "\n";
            if (rows.empty())
            {
                out += "  (no seams)\n";
                return;
            }
            for (const SeamAggregate& row : rows)
            {
                out += "  " + padded(row.mLabel, 16) + " seams " + count(row.mSeams) + ", touching "
                    + count(row.mTouching) + ", pairs " + count(row.mPairs) + ", within " + ladderText(row.mWithin)
                    + "\n";
                if (row.mAnyPairs > 0)
                {
                    out += "    all vertices: seams with pairs " + count(row.mAnyTouching) + ", pairs "
                        + count(row.mAnyPairs) + ", within " + ladderText(row.mAnyWithin) + ", distance min/median "
                        + number(row.mMinAnyDistance) + " / " + number(row.mMedianAnyDistance) + "\n";
                }
                if (row.mTouching == 0)
                    continue;
                out += "    distance min/median/max " + number(row.mMinDistance) + " / " + number(row.mMedianDistance)
                    + " / " + number(row.mMaxDistance) + "; gap min/median/max " + number(row.mMinGap) + " / "
                    + number(row.mMedianGap) + " / " + number(row.mMaxGap) + "; overlap/meeting/gap "
                    + count(row.mOverlapPairs) + "/" + count(row.mMeetingPairs) + "/" + count(row.mGapPairs) + "\n";
                out += "    weight disagreement max " + number(row.mMaxWeightDisagreement) + "; bone sets differ "
                    + count(row.mBoneSetDiffers) + "; uv differs " + count(row.mUvDiffers) + "; normal angle max "
                    + number(row.mMaxNormalAngle) + "\n";
            }
        }
    }

    void computeTotals(UnitReport& unit)
    {
        unit.mVertices = 0;
        unit.mTriangles = 0;
        unit.mRigidParts = 0;
        unit.mSkinnedParts = 0;
        std::set<std::string> bones, materials;
        std::map<std::string, std::uint64_t> files;
        for (const SlotReport& slot : unit.mSlots)
        {
            if (!slot.mRead)
                continue;
            unit.mVertices += slot.mStats.mVertices;
            unit.mTriangles += slot.mStats.mTriangles;
            if (slot.mStats.mSkinned)
                ++unit.mSkinnedParts;
            else
                ++unit.mRigidParts;
            bones.insert(slot.mBoneNames.begin(), slot.mBoneNames.end());
            materials.insert(slot.mMaterials.begin(), slot.mMaterials.end());
            files.emplace(slot.mModel, slot.mFileBytes);
        }
        unit.mBones = bones.size();
        unit.mMaterials = materials.size();
        unit.mFiles = files.size();
        unit.mFileBytes = 0;
        for (const auto& [model, bytes] : files)
            unit.mFileBytes += bytes;
    }

    std::vector<SeamAggregate> aggregateSeams(const AnalysisReport& report, Group group)
    {
        struct Row
        {
            SeamAggregate mAggregate;
            std::vector<double> mMins, mMedians, mMaxes, mGapMins, mGapMedians, mGapMaxes, mAnyMins, mAnyMedians;
        };
        std::vector<Row> rows;
        const auto rowFor = [&rows](const std::string& label) -> Row& {
            for (Row& row : rows)
            {
                if (row.mAggregate.mLabel == label)
                    return row;
            }
            rows.emplace_back();
            rows.back().mAggregate.mLabel = label;
            return rows.back();
        };
        const auto add = [&rowFor](const std::string& label, const SeamReport& r) {
            Row& row = rowFor(label);
            SeamAggregate& a = row.mAggregate;
            ++a.mSeams;
            if (r.mAnyDistance.mCount > 0)
            {
                ++a.mAnyTouching;
                a.mAnyPairs += r.mAnyDistance.mCount;
                for (std::size_t i = 0; i < a.mAnyWithin.size(); ++i)
                    a.mAnyWithin[i] += r.mAnyWithin[i];
                row.mAnyMins.push_back(r.mAnyDistance.mMin);
                row.mAnyMedians.push_back(r.mAnyDistance.mMedian);
            }
            if (!r.mTouches)
                return;
            ++a.mTouching;
            a.mPairs += r.mDistance.mCount;
            for (std::size_t i = 0; i < a.mWithin.size(); ++i)
                a.mWithin[i] += r.mWithin[i];
            row.mMins.push_back(r.mDistance.mMin);
            row.mMedians.push_back(r.mDistance.mMedian);
            row.mMaxes.push_back(r.mDistance.mMax);
            row.mGapMins.push_back(r.mSignedGap.mMin);
            row.mGapMedians.push_back(r.mSignedGap.mMedian);
            row.mGapMaxes.push_back(r.mSignedGap.mMax);
            a.mOverlapPairs += r.mOverlapPairs;
            a.mGapPairs += r.mGapPairs;
            a.mMeetingPairs += r.mMeetingPairs;
            if (r.mWeightDisagreement.mCount > 0)
                a.mMaxWeightDisagreement = std::max(a.mMaxWeightDisagreement, r.mWeightDisagreement.mMax);
            a.mBoneSetDiffers += r.mBoneSetDiffers;
            a.mUvDiffers += r.mUvDiffers;
            if (r.mNormalAngle.mCount > 0)
                a.mMaxNormalAngle = std::max(a.mMaxNormalAngle, r.mNormalAngle.mMax);
        };

        for (const UnitReport& unit : report.mUnits)
        {
            if (unit.mBeast != (group == Group::Beast))
                continue;
            for (const SeamEntry& seam : unit.mSeams)
                add(seam.mLabel, seam.mReport);
            for (const HeadEntry& head : unit.mHeads)
            {
                if (head.mRead)
                    add("head-neck", head.mNeckSeam);
            }
        }

        std::vector<SeamAggregate> result;
        for (Row& row : rows)
        {
            SeamAggregate& a = row.mAggregate;
            if (!row.mAnyMins.empty())
            {
                a.mMinAnyDistance = *std::min_element(row.mAnyMins.begin(), row.mAnyMins.end());
                a.mMedianAnyDistance = distribution(row.mAnyMedians).mMedian;
            }
            if (!row.mMins.empty())
            {
                a.mMinDistance = *std::min_element(row.mMins.begin(), row.mMins.end());
                a.mMedianDistance = distribution(row.mMedians).mMedian;
                a.mMaxDistance = *std::max_element(row.mMaxes.begin(), row.mMaxes.end());
                a.mMinGap = *std::min_element(row.mGapMins.begin(), row.mGapMins.end());
                a.mMedianGap = distribution(row.mGapMedians).mMedian;
                a.mMaxGap = *std::max_element(row.mGapMaxes.begin(), row.mGapMaxes.end());
            }
            result.push_back(a);
        }
        return result;
    }

    std::string toJson(const AnalysisReport& report)
    {
        Json json;
        json.beginObject();
        json.field("format", "openeden.bodyanalysis");
        json.field("version", std::size_t{ 1 });
        json.key("units");
        json.beginArray();
        for (const UnitReport& unit : report.mUnits)
        {
            json.beginObject();
            json.field("race", unit.mRace);
            json.field("skeleton", unit.mSkeleton);
            json.field("skeletonBones", unit.mSkeletonBones);
            json.field("female", unit.mFemale);
            json.field("beast", unit.mBeast);
            json.field("playable", unit.mPlayable);
            json.key("totals");
            json.beginObject();
            json.field("vertices", unit.mVertices);
            json.field("triangles", unit.mTriangles);
            json.field("bones", unit.mBones);
            json.field("materials", unit.mMaterials);
            json.field("files", unit.mFiles);
            json.field("fileBytes", unit.mFileBytes);
            json.field("skinnedParts", unit.mSkinnedParts);
            json.field("rigidParts", unit.mRigidParts);
            json.endObject();
            json.key("slots");
            json.beginArray();
            for (const SlotReport& slot : unit.mSlots)
            {
                json.beginObject();
                json.field("slot", slot.mSlot);
                json.field("record", slot.mRecord);
                json.field("model", slot.mModel);
                json.field("fileBytes", slot.mFileBytes);
                json.field("read", slot.mRead);
                json.field("fileShapes", slot.mFileShapes);
                json.key("errors");
                json.stringArray(slot.mErrors);
                writeStats(json, slot.mStats);
                json.key("shapeNames");
                json.stringArray(slot.mShapeNames);
                json.key("boneNames");
                json.stringArray(slot.mBoneNames);
                json.key("materials");
                json.stringArray(slot.mMaterials);
                json.field("allIdentityTransforms", slot.mAllIdentity);
                json.field("anyMirrored", slot.mAnyMirrored);
                json.field("placedByBone", slot.mPlaced);
                json.endObject();
            }
            json.endArray();
            json.key("seams");
            json.beginArray();
            for (const SeamEntry& seam : unit.mSeams)
            {
                json.beginObject();
                json.field("label", seam.mLabel);
                json.field("slotA", seam.mSlotA);
                json.field("slotB", seam.mSlotB);
                writeSeamFields(json, seam.mReport);
                json.endObject();
            }
            json.endArray();
            json.key("heads");
            json.beginArray();
            for (const HeadEntry& head : unit.mHeads)
            {
                json.beginObject();
                json.field("record", head.mRecord);
                json.field("model", head.mModel);
                json.field("read", head.mRead);
                writeStats(json, head.mStats);
                json.key("neckSeam");
                json.beginObject();
                writeSeamFields(json, head.mNeckSeam);
                json.endObject();
                json.endObject();
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();
        json.key("aggregate");
        json.beginObject();
        json.key("humanoid");
        writeAggregate(json, aggregateSeams(report, Group::Humanoid));
        json.key("beast");
        writeAggregate(json, aggregateSeams(report, Group::Beast));
        json.endObject();
        json.endObject();
        return json.finish();
    }

    std::string toText(const AnalysisReport& report)
    {
        std::string out
            = "OpenEden stock body analysis (task 0069, stage a)\n"
              "Units are Morrowind units. Distances are between a boundary vertex of one part and its "
              "nearest boundary vertex of the other; the ladder counts pairs at or under 0, 1e-4, 1e-3, "
              "1e-2, 0.1 and 0.5.\n";
        for (const UnitReport& unit : report.mUnits)
        {
            out += "\n== " + unit.mRace + (unit.mFemale ? " female" : " male") + " ("
                + (unit.mBeast ? "beast" : "humanoid") + (unit.mPlayable ? ", playable" : "") + ") ==\n";
            out += "  skeleton: " + unit.mSkeleton + " (" + count(unit.mSkeletonBones) + " named nodes)\n";
            out += "  totals: vertices " + count(unit.mVertices) + ", triangles " + count(unit.mTriangles) + ", bones "
                + count(unit.mBones) + ", materials " + count(unit.mMaterials) + ", files " + count(unit.mFiles) + " ("
                + std::to_string(unit.mFileBytes) + " bytes), skinned parts " + count(unit.mSkinnedParts)
                + ", rigid parts " + count(unit.mRigidParts) + "\n";
            for (const SlotReport& slot : unit.mSlots)
            {
                out += "  slot " + padded(slot.mSlot, 10);
                if (slot.mRecord.empty())
                {
                    out += " no record\n";
                    continue;
                }
                out += " record '" + slot.mRecord + "' model '" + slot.mModel + "'";
                if (!slot.mRead)
                {
                    out += " NOT READ";
                    for (const std::string& error : slot.mErrors)
                        out += "; " + error;
                    out += "\n";
                    continue;
                }
                out += ": " + count(slot.mStats.mShapes) + " of " + count(slot.mFileShapes) + " shapes, vertices "
                    + count(slot.mStats.mVertices) + ", triangles " + count(slot.mStats.mTriangles) + ", "
                    + (slot.mStats.mSkinned ? "skinned" : "rigid") + ", bones " + count(slot.mStats.mBones)
                    + ", influences 0..4 "
                    + ladderText({ slot.mStats.mInfluenceHistogram[0], slot.mStats.mInfluenceHistogram[1],
                        slot.mStats.mInfluenceHistogram[2], slot.mStats.mInfluenceHistogram[3],
                        slot.mStats.mInfluenceHistogram[4], 0 })
                    + ", over-cut " + count(slot.mStats.mOverCut) + ", weight sum off "
                    + count(slot.mStats.mWeightSumOff) + ", duplicates " + count(slot.mStats.mDuplicatePositions) + " ("
                    + count(slot.mStats.mDuplicateSplits) + " splits), boundary " + count(slot.mStats.mBoundaryVertices)
                    + ", edges open/shared/over " + count(slot.mStats.mOpenEdges) + "/"
                    + count(slot.mStats.mSharedEdges) + "/" + count(slot.mStats.mOverUsedEdges) + ", bounds ("
                    + number(slot.mStats.mBoundsMin[0]) + " " + number(slot.mStats.mBoundsMin[1]) + " "
                    + number(slot.mStats.mBoundsMin[2]) + ")-(" + number(slot.mStats.mBoundsMax[0]) + " "
                    + number(slot.mStats.mBoundsMax[1]) + " " + number(slot.mStats.mBoundsMax[2]) + ")"
                    + (slot.mAllIdentity ? "" : ", transforms not identity") + (slot.mAnyMirrored ? ", mirrored" : "")
                    + (slot.mPlaced ? ", placed by bone" : "") + "\n";
                for (const std::string& error : slot.mErrors)
                    out += "    error: " + error + "\n";
            }
            for (const SeamEntry& seam : unit.mSeams)
                seamText(out, seam.mLabel + " (" + seam.mSlotA + "|" + seam.mSlotB + ")", seam.mReport);
            out += "  head records: " + count(unit.mHeads.size()) + "\n";
            for (const HeadEntry& head : unit.mHeads)
            {
                if (!head.mRead)
                {
                    out += "  head '" + head.mRecord + "' NOT READ\n";
                    continue;
                }
                seamText(out, "head-neck '" + head.mRecord + "'", head.mNeckSeam);
            }
        }
        aggregateText(out, "Humanoid seams (all units pooled)", aggregateSeams(report, Group::Humanoid));
        aggregateText(out, "Beast seams (all units pooled)", aggregateSeams(report, Group::Beast));
        return out;
    }
}
