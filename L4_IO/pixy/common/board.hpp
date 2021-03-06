#ifndef BOARD_HPP
#define BOARD_HPP

#include <vector>
#include <utility>
#include <queue>

#include "printf_lib.h"

#include "pixy/config.hpp"
#include "pixy/common.hpp"
#include "pixy/common/chip.hpp"
#include "pixy/common/corners.hpp"
#include "pixy/common/point.hpp"
#include "pixy/common/stat.hpp"

namespace team9
{
namespace pixy
{

class Board_t
{
    public:
        enum PrintMode_t {LOCATION, COLOR, OPENCV_META};
        enum RowOrCol_t {ROW, COL};

        Board_t() :
            xWatchedChips(7),
            xWatchedCols(7, 0),
            ulRows(6),
            ulCols(7),
            ulDistBetweenRows(0),
            ulDistBetweenCols(0),
            xPointEMA_alpha(CHIP_LOC_EMA_ALPHA),
            xTolerance(CHIP_PROXIM_TOLERANCE)
        {}

        Board_t(const Corners_t& xCorners, const ChipColor_t eHumanChipColor) :
                xWatchedChips(7),
                xWatchedCols(7, 0),
                ulRows(6),
                ulCols(7),
                ulDistBetweenRows(0),
                ulDistBetweenCols(0),
                xPointEMA_alpha(CHIP_LOC_EMA_ALPHA),
                xTolerance(CHIP_PROXIM_TOLERANCE)
        {
            vInitStringMap();
            vBuildGrid(xCorners, eHumanChipColor);
        }

        void vAdjustEMAAlpha(bool bEMAAlphaUp)
        {
            xPointEMA_alpha = (bEMAAlphaUp) ?
                    std::min(xPointEMA_alpha + EMA_ALPHA_ADJ, 0.9999f):
                    std::max(xPointEMA_alpha - EMA_ALPHA_ADJ, 0.0001f);
        }

        int vBuildGrid (const Corners_t& xCorners_arg, const ChipColor_t eHumanChipColor)
        {
            std::vector<Point_t<float>> xTLBLPoints;
            std::vector<Point_t<float>> xTRBRPoints;
            Point_t<float>::xPointsOnLine(xCorners_arg(TOP_LEFT),
                                          xCorners_arg(BOT_LEFT), 6,
                                          xTLBLPoints);
            Point_t<float>::xPointsOnLine(xCorners_arg(TOP_RIGHT),
                                          xCorners_arg(BOT_RIGHT), 6,
                                          xTRBRPoints);
            std::ostringstream oss;
            oss << "xTLBLPoints:\n";
            for (auto& pt : xTLBLPoints)
            {
                oss << pt.xStr() << "\n";
            }
            oss << "xTRBRPoints:\n";
            for (auto& pt : xTRBRPoints)
            {
                oss << pt.xStr() << "\n";
            }
            printf("\n%s\n", oss.str().c_str());
            u0_dbg_printf("\n%s\n", oss.str().c_str());
            ulDistBetweenRows = Point_t<float>::xCalcDist(
                    xCorners_arg(TOP_LEFT), xCorners_arg(BOT_LEFT)) / ulRows;
            ulDistBetweenCols = Point_t<float>::xCalcDist(
                    xCorners_arg(TOP_LEFT), xCorners_arg(TOP_RIGHT)) / ulCols;
            return xFillBoard(xTLBLPoints, xTRBRPoints, eHumanChipColor);
        }

        int xFillBoard(std::vector<Point_t<float>>& xTLBLPoints,
                       std::vector<Point_t<float>>& xTRBRPoints,
                       ChipColor_t eHumanChipColor)
        {
            if ((int)xTLBLPoints.size() != ulRows ||
                (int)xTRBRPoints.size() != ulRows)
            {
                return 1; // Invalid number of points
            }
            int xRightPtIdx = 0;
            xAllChips.clear();
            xAllChips.resize(ulRows * ulCols);
            int lPtIdx = 0;
            for (Point_t<float>& xLeftPt : xTLBLPoints)
            {
                std::vector<Point_t<float>> xPts;
                Point_t<float>& xRightPt = xTRBRPoints[xRightPtIdx++];
                Point_t<float>::xPointsOnLine(xLeftPt, xRightPt, ulCols, xPts);
                for (auto& xPt : xPts)
                {
                    Chip_t xChip(xPointEMA_alpha, eHumanChipColor);
                    xChip.xPtLoc.vUpdate(xPt);
                    xAllChips[lPtIdx++] = xChip;
                }
            }
            return (lPtIdx == ulRows * ulCols) ? 0 : 2;
        }

        void vCalcSeenChips(std::vector<Block_t>& xBlocks,
                std::vector<std::pair<int, ChipColor_t>>& xSeenChips)
        {
            switch (eSeenChipAlgo)
            {
                case STUPID: vChipAlgoStupid(xBlocks, xSeenChips); break;
                case DOWN_RIGHT: vChipAlgoDownRight(xBlocks, xSeenChips); break;
                default: printf("Error selecting seen chip algo.\n");
                         u0_dbg_printf("Error selecting seen chip algo.\n"); break;
            }
        }

        void vChipAlgoStupid(std::vector<Block_t>& xBlocks,
                std::vector<std::pair<int, ChipColor_t>>& xSeenChips)
        {
            float xMaxDist = (float)ulDistBetweenCols * xTolerance;
            for (int lCol = 0; lCol < (int)xWatchedChips.size(); ++lCol)
            {
                int xRow = xWatchedCols[lCol];
                if (xRow < 0 || xRow > (int)ulRows)
                {
                    continue;
                }
                int xIdx = (ulRows-xRow-1) * ulCols + lCol;
                Chip_t& xChip = xAllChips[xIdx];
                ChipColor_t xTempColor = NONE;
                float xTempDist = 9999;
                for (int lJ = 0; lJ < (int)xBlocks.size(); ++lJ)
                {
                    Point_t<uint16_t>& xBlockPt = xBlocks[lJ].xPoint;
                    const Point_t<float>& xChipPt = xChip.xPtLoc.xPoint();
                    float xDist = Point_t<float>::xCalcDist(xChipPt, xBlockPt);
                    if (xDist < xTempDist)
                    {
                        xTempColor = (ChipColor_t)xBlocks[lJ].usSignature;
                        xTempDist = xDist;
                    }
                }
                if (xTempDist < xMaxDist)
                {
                    xSeenChips.push_back(std::make_pair(xIdx, xTempColor));
                }
            }
        }

        void vChipAlgoDownRight(std::vector<Block_t>& xBlocks,
                std::vector<std::pair<int, ChipColor_t>>& xSeenChips)
        {
            for (Block_t& xBlock : xBlocks)
            {
                bool bDone = false;
                float xDistCurr = 0.0;
                size_t xIdx = 0;
                while (!bDone)
                {
                    size_t xRow = xIdx / 7;
                    size_t lCol = xIdx % 7;
                    bool bCheckRight = true;
                    bool bCheckDown = true;
                    if (xRow == 6 || lCol == 7)
                    {
                        bDone = true;
                        break;
                    }
                    bCheckRight = (lCol <= 5);
                    bCheckDown = (xRow <= 4);
                    // The following 10 lines tells me (functional) refactoring
                    // is in order
                    const Point_t<float>& xThisPt =
                            xAllChips[xIdx].xPtLoc.xPoint();
                    const Point_t<float>& xRPt =
                            xAllChips[xIdx+1].xPtLoc.xPoint();
                    const Point_t<float>& xDPt =
                            xAllChips[xIdx+1].xPtLoc.xPoint();
                    xDistCurr = xThisPt.xCalcDist(xBlock.xPoint);
                    float xDistRight = bCheckRight ?
                                       xRPt.xCalcDist(xBlock.xPoint) : 0.0f;
                    float xDistDown = bCheckDown ?
                                      xDPt.xCalcDist(xBlock.xPoint) : 0.0f;
                    if (bCheckRight && bCheckRight)
                    {
                        if (xDistCurr < xDistRight && xDistCurr < xDistDown)
                        {
                            bDone = true;
                            break;
                        }
                        xIdx += (xDistRight < xDistDown) ? 1 : 7;
                    }
                    else if (bCheckRight) // Don't check down, row == 5
                    {
                        if (xDistCurr < xDistRight)
                        {
                            bDone = true;
                            break;
                        }
                        xIdx += 1;
                    }
                    else if (bCheckDown) // Don't check right, col == 6
                    {
                        if (xDistCurr < xDistDown)
                        {
                            bDone = true;
                            break;
                        }
                        xIdx += 7;
                    }
                    else
                    {
                        bDone = true;
                    }
                }
                if (xDistCurr < ulDistBetweenCols * xTolerance)
                {
                    xSeenChips.push_back(
                        std::make_pair(xIdx, (ChipColor_t)xBlock.usSignature));
                }
            }
        }

        void vUpdate(std::vector<std::pair<int, ChipColor_t>>& xSeenChips)
        {
            for (int lCol = 0; lCol < (int)xWatchedCols.size(); ++lCol)
            {
                if (xWatchedCols[lCol] < 0)
                {
                    continue;
                }
                int lNoneCnt = 0;
                int lGreenCnt = 0;
                int lRedCnt = 0;
                int lRow = xWatchedCols[lCol];
                for (auto& xSeenChip : xSeenChips)
                {
                    if (!bInBounds<ROW>(lRow))
                    {
                        printf("Warning, lRow in vUpdate is: %d, xWatchedCols[%d]: %d\n",
                               lRow, lCol, xWatchedCols[lCol]);
                        u0_dbg_printf("Warning, lRow in vUpdate is: %d, xWatchedCols[%d]: %d\n",
                                      lRow, lCol, xWatchedCols[lCol]);
                    }
                    if (xSeenChip.first == lBoardIdx(lRow, lCol))
                    {
                        switch (xSeenChip.second)
                        {
                            case NONE: lNoneCnt++; break;
                            case GREEN: lGreenCnt++; break;
                            case RED: lRedCnt++; break;
                        }
                    }
                }
                if (lNoneCnt || lGreenCnt || lRedCnt)
                {
                    printf("Seen in row %d, col: %d\t[N: %d\tG: %d\tR: %d]\n", lRow, lCol, lNoneCnt, lGreenCnt, lRedCnt);
                    u0_dbg_printf("Seen in row %d, col: %d\t[N: %d\tG: %d\tR: %d]\n", lRow, lCol, lNoneCnt, lGreenCnt, lRedCnt);
                    xWatchedChips[lCol].vUpdateFreq(lNoneCnt, lGreenCnt, lRedCnt);
                }
            }
        }

        int lColChanged()
        {
            // Poll all of the watched chips (indexes stored in xWatchedCols)
            // asking if they are "not none and the same N times"

            // For every entry in size 7 vector of "watched col heights"
            // (ex: [-1 0 0 0 0 0 -1])
            int lCols = xWatchedCols.size();
            printf("Now watching (lCols: %d): ", lCols);
            u0_dbg_printf("Now watching (lCols: %d): ", lCols);

            for (int lCol = 0; lCol < lCols; ++lCol)
            {

                // Get current column height/rows
                int lRow = xWatchedCols[lCol];

                printf(" %d", lRow);
                u0_dbg_printf(" %d", lRow);

                if (!bInBounds<ROW>(lRow)) continue;

                // Dereference chip in current column (xWatchedChips same
                // size as xWatchedCols
                Chip_t& xChip = xWatchedChips[lCol];

                // Ask if it has been the same for the last two samples
                if (xChip.bChipKnown())
                {
                    xAllChips[lBoardIdx(lRow, lCol)].vSet(xChip.xMaxChip());
                    xWatchedChips[lCol].vResetCounters();
                    xWatchedCols[lCol]++;
                    return lCol;
                }
            }
            printf("\n");
            u0_dbg_printf("\n");

            return -1;
        }

        int lInsert(PixyCmd_t& xInsertCmd)
        {
            int lCol = xInsertCmd.lColumn;
            printf("lCol: %d\n", lCol);
            u0_dbg_printf("lCol: %d\n", lCol);

            if (bInBounds<COL>(lCol))
            {
                ChipColor_t xChipColor = (ChipColor_t)xInsertCmd.lColor;
                int lRow = xWatchedCols[lCol];
                printf("lRow: %d\n", lRow);
                u0_dbg_printf("lRow: %d\n", lRow);
                if (!bInBounds<ROW>(lRow)) return -1;
                xAllChips[lBoardIdx(lRow, lCol)].vSet(xChipColor);
                xWatchedChips[lCol].vResetCounters();
                xWatchedCols[lCol]++;
                return lRow + 1;
            }
            return -2;
        }

        template <RowOrCol_t R_O_C>
        bool bInBounds(int lIdx) const
        {
            bool bReturn = lIdx >= 0;
            switch(R_O_C)
            {
                case ROW: { bReturn = (lIdx <= ulRows - 1) && bReturn; break; }
                case COL: { bReturn = (lIdx <= ulCols - 1) && bReturn; break; }
            }
            return bReturn;
        }

        int lBoardIdx(const int lRow, const int lCol) const
        {
            return (ulRows - lRow - 1) * ulCols + lCol;
        }

        int lRowFromIdx(const int lBoardIdx_arg) const
        {
            return (ulRows - (lBoardIdx_arg / ulCols) - 1);
        }

        uint32_t ulExpectedTotalChips() const
        {
            return ulRows * ulCols;
        }

        uint32_t ulActualTotalChips() const
        {
            return xAllChips.size();
        }

        float xGetAlpha() const
        {
            return xPointEMA_alpha;
        }

        void vPrintChips(
                PrintMode_t xPrintStyle = LOCATION,
                const std::vector<std::pair<int, ChipColor_t>> xSeenChips =
                      std::vector<std::pair<int, ChipColor_t>>())
        {
            if ((int)xAllChips.size() != ulRows * ulCols)
            {
                printf("Chip print error: Num chips: %d, rows: %d, cols: %d\n",
                        xAllChips.size(), ulRows, ulCols);
                u0_dbg_printf("Chip print error: Num chips: %d, rows: %d, cols: %d\n",
                            xAllChips.size(), ulRows, ulCols);
                return;
            }
            switch (xPrintStyle)
            {
                case LOCATION: vLocationPrint(); break;
                case COLOR: vColorPrint(xSeenChips); break;
                case OPENCV_META: vOpenCVPrint(); break;
            }
        }

        void vLocationPrint()
        {
            std::ostringstream xOss;
            int lIdx = 0;
            for (int lI = ulRows-1; lI >= 0; --lI)
             {
                 xOss << "Row: " << lI;
                 for (int lJ = 0; lJ < ulCols; ++lJ)
                 {
                     xOss << " " << xAllChips[lIdx++].xLocStr();
                 }
                 xOss << "\n";
             }
            printf("%s\n", xOss.str().c_str());
            u0_dbg_printf("%s\n", xOss.str().c_str());
        }

        std::string xChipColorRow(std::ostringstream& xOss,
                                  ChipColor_t lCol0, ChipColor_t lCol1,
                                  ChipColor_t lCol2, ChipColor_t lCol3,
                                  ChipColor_t lCol4, ChipColor_t lCol5,
                                  ChipColor_t lCol6)
        {
            xOss << xStringMap[lCol0] << " "
                 << xStringMap[lCol1] << " "
                 << xStringMap[lCol2] << " "
                 << xStringMap[lCol3] << " "
                 << xStringMap[lCol4] << " "
                 << xStringMap[lCol5] << " "
                 << xStringMap[lCol6];
            std::string xRowStr(xOss.str());
            xOss.str("");
            xOss.clear();
            return xRowStr;
        }

        void vColorPrint(
                const std::vector<std::pair<int, ChipColor_t>> xSeenChips =
                      std::vector<std::pair<int, ChipColor_t>>(),
                bool bPrintStdDev = false)
        {
            std::ostringstream xOss;
            int lIdx1 = 0;
            int lIdx2 = 0;
            xOss << "Col:   0 1 2 3 4 5 6            0 1 2 3 4 5 6\n";
            for (int xI = (int)ulRows - 1; xI >= 0; --xI)
            {
               if (!xSeenChips.empty())
               {
                    xOss << "Row " << xI << ":";
                    //// Print incoming chip colors
                    for (int xJ = 0; xJ < (int)ulCols; ++xJ)
                    {
                       ChipColor_t xChipColor = NONE;
                       for (auto& xChipColorPair : xSeenChips)
                       {
                           if (xChipColorPair.first == lIdx1)
                           {
                               xChipColor = xChipColorPair.second;
                           }
                       }
                       lIdx1++;
                       xOss << " " << xStringMap[xChipColor];
                    }
                    xOss << "     ";
                }
                Chip_t& xChipRowICol0 = xAllChips[lIdx2++];
                Chip_t& xChipRowICol1 = xAllChips[lIdx2++];
                Chip_t& xChipRowICol2 = xAllChips[lIdx2++];
                Chip_t& xChipRowICol3 = xAllChips[lIdx2++];
                Chip_t& xChipRowICol4 = xAllChips[lIdx2++];
                Chip_t& xChipRowICol5 = xAllChips[lIdx2++];
                Chip_t& xChipRowICol6 = xAllChips[lIdx2++];
                xOss << "Row: " << xI << " "
                     << xStringMap[xChipRowICol0.xMaxChip()] << " "
                     << xStringMap[xChipRowICol1.xMaxChip()] << " "
                     << xStringMap[xChipRowICol2.xMaxChip()] << " "
                     << xStringMap[xChipRowICol3.xMaxChip()] << " "
                     << xStringMap[xChipRowICol4.xMaxChip()] << " "
                     << xStringMap[xChipRowICol5.xMaxChip()] << " "
                     << xStringMap[xChipRowICol6.xMaxChip()] << "\n";
            }
            printf("%s\n", xOss.str().c_str());
            u0_dbg_printf("%s\n", xOss.str().c_str());
        }

        void vOpenCVPrint()
        {
            std::ostringstream xOss;
            xOss << "std::vector<cv::Point2f> point_vec;\n";
            for (auto& xChip : xAllChips)
            {
                xOss << "point_vec.push_back("
                     << Point_t<float>::xOpenCVPt(
                        xChip.xPtLoc.xPoint()) << ");\n";
            }
            printf("%s\n", xOss.str().c_str());
            u0_dbg_printf("%s\n", xOss.str().c_str());
        }

        void vPrintFillStatus()
        {
            std::ostringstream xOss;
            for (int xI = ulRows-1; xI >= 0; --xI)
            {
                xOss << "Row " << xI << ":";
                for (int xJ = 0; xJ < (int)ulCols; ++xJ)
                {
                    if (xWatchedCols[xJ] == (int)xI)
                    {
                        xOss << " " << xI;
                    }
                    else
                    {
                        xOss << " -";
                    }
                }
                xOss << "\n";
            }

            if (xOss.str().length() == 0)
            {
                xOss.str("");
                xOss.clear();
                return;
            }
            printf("%s\n", xOss.str().c_str());
            u0_dbg_printf("%s\n", xOss.str().c_str());
        }

    private:
        void vInitStringMap()
        {
            xStringMap[NONE] = "-";
            xStringMap[GREEN] = "G";
            xStringMap[RED] = "R";
        }

        std::vector<Chip_t> xAllChips;
        std::vector<Chip_t> xWatchedChips;
        std::vector<int> xWatchedCols;

        std::map<ChipColor_t, std::string> xStringMap;

        int ulRows;
        int ulCols;
        int ulDistBetweenRows;
        int ulDistBetweenCols;
        float xPointEMA_alpha;
        float xTolerance;
};

} // namespace pixy
} // namespace team9

#endif
