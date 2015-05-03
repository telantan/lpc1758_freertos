#ifndef PIXY_EYES_HPP
#define PIXY_EYES_HPP

#include <vector>

#include "ssp1.h"
#include "utilities.h"
#include "printf_lib.h"

#include "pixy/pixy_common.hpp"

namespace team9
{
namespace pixy
{

class PixyEyes_t
{
public:

    enum State_t {START, READ_FIRST, READ_BLOCK, READ_NEXT, DONE} eState;
    enum BlockType_t {NORMAL, COLOR_CODED} eBlockType;

    PixyEyes_t(uint32_t ulMaxBlocks_arg) :
        eState(START),
        ulMaxBlocks(ulMaxBlocks_arg),
        ulBlockCount(0),
        usChecksum(0x0000),
        usRecvLast(0xffff)
    {
        vInitMapStr();
    }

    __inline uint16_t usGetShort()
    {
        uint16_t usRecv = ssp1_exchange_byte(0x5a); // Sync byte
        usRecv <<= 8;
        return usRecv |= ssp1_exchange_byte(0);
    }

    uint32_t ulSeenBlocks(std::vector<Block_t>& vRecvBlocks)
    {
        ulBlockCount = 0;
        usChecksum = 0x0000;
        usRecvLast = 0xffff;

        vRecvBlocks.clear();
        vRecvBlocks.resize(ulMaxBlocks);

        while (eState != DONE)
        {
            std::cout << "State: " << xStringMap[eState] << std::endl;
            switch (eState)
            {
            case START:
            {
                uint16_t usRecv = usGetShort();
                if (usRecv == 0xaa55 && usRecvLast == 0xaa55)
                {
                    eBlockType = NORMAL;
                    eState = READ_FIRST;
                }
                else if (usRecv == 0xaa56 && usRecvLast == 0xaa55)
                {
                    eBlockType = COLOR_CODED;
                    eState = READ_FIRST;
                }
                else if (usRecv == 0x55aa)
                {
                    ssp1_exchange_byte(0); // out of sync
                    eState = START;
                }
                else
                {
                    eState = START;
                }
                usRecvLast = usRecv;
                //		            	vStateStart();
                break;
            }
            case READ_FIRST:
            {
                uint16_t usRecv = usGetShort();
                switch (usRecv)
                {
                case 0xaa55:
                {
                    eBlockType = NORMAL;
                    eState = DONE;
                    break;
                }
                case 0xaa56:
                {
                    eBlockType = COLOR_CODED;
                    eState = DONE;
                    break;
                }
                case 0x0000:
                {
                    eState = START;
                    break;
                }
                default:
                {
                    usChecksum = usRecv;
                    eState = READ_BLOCK;
                    break;
                }
                }
                usRecvLast = usRecv;
                //		            	vStateReadFirst();
                break;
            }
            case READ_BLOCK:
            {

                Block_t xBlock;
                xBlock.usSignature = usGetShort();
                xBlock.xPoint.usX = usGetShort();
                xBlock.xPoint.usY = usGetShort();
                xBlock.usWidth= usGetShort();
                xBlock.usHeight = usGetShort();
                xBlock.usAngle = (eBlockType == NORMAL) ? 0 : usGetShort();
                usRecvLast = (eBlockType == NORMAL) ? xBlock.usHeight :
                        xBlock.usAngle;

                uint16_t usBlockSum = xBlock++;
                if (usChecksum == usBlockSum)
                {
                    vRecvBlocks[ulBlockCount] = xBlock;
                    ulBlockCount++;
                }
                else
                {
                    u0_dbg_printf("usChecksum error!\t"
                            "Checksum %d\tBlocksum: %d\n",
                            usChecksum, usBlockSum);
                }
                eState = (ulBlockCount == ulMaxBlocks) ? DONE : READ_NEXT;

                //		            	vStateReadBlock(vRecvBlocks);
                break;
            }
            case READ_NEXT:
            {
                uint16_t usRecv = usGetShort();
                switch (usRecv)
                {
                case 0xaa55:
                {
                    eBlockType = NORMAL;
                    eState = READ_FIRST;
                    break;
                }
                case 0xaa56:
                {
                    eBlockType = COLOR_CODED;
                    eState = READ_FIRST;
                    break;
                }
                default:
                {
                    eState = START;
                    break;
                }
                }
                usRecvLast = usRecv;
                //		            	vStateReadNext();
                break;
            }
            default:
            {
                u0_dbg_printf("Error: usGetBlocks default case\n");
                break;
            }
            }
        }
        vRecvBlocks.resize(ulBlockCount);
        eState = START;
        return ulBlockCount;
    }


    //		__inline void vStateStart()
    //		{
    //			uint16_t usRecv = usGetShort();
    //			if (usRecv == 0xaa55 && usRecvLast == 0xaa55)
    //			{
    //				eBlockType = NORMAL;
    //				eState = READ_FIRST;
    //			}
    //			else if (usRecv == 0xaa56 && usRecvLast == 0xaa55)
    //			{
    //				eBlockType = COLOR_CODED;
    //				eState = READ_FIRST;
    //			}
    //			else if (usRecv == 0x55aa)
    //			{
    //				ssp1_exchange_byte(0); // out of sync
    //				eState = START;
    //			}
    //			else
    //			{
    //				eState = START;
    //			}
    //			usRecvLast = usRecv;
    //		}

    //		__inline void vStateReadFirst()
    //		{
    //            uint16_t usRecv = usGetShort();
    //            switch (usRecv)
    //            {
    //                case 0xaa55:
    //                {
    //                    eBlockType = NORMAL;
    //                    eState = DONE;
    //                    break;
    //                }
    //                case 0xaa56:
    //                {
    //                    eBlockType = COLOR_CODED;
    //                    eState = DONE;
    //                    break;
    //                }
    //                case 0x0000:
    //                {
    //                    eState = START;
    //                    break;
    //                }
    //                default:
    //                {
    //                    usChecksum = usRecv;
    //                    eState = READ_BLOCK;
    //                    break;
    //                }
    //            }
    //            usRecvLast = usRecv;
    //		}

    //		__inline void vStateReadBlock(std::vector<Block_t>& vRecvBlocks)
    //		{
    //			Block_t xBlock;
    //			xBlock.usSignature = usGetShort();
    //			xBlock.xPoint.usX = usGetShort();
    //			xBlock.xPoint.usY = usGetShort();
    //			xBlock.usWidth= usGetShort();
    //			xBlock.usHeight = usGetShort();
    //			xBlock.usAngle = (eBlockType == NORMAL) ? 0 : usGetShort();
    //			usRecvLast = (eBlockType == NORMAL) ? xBlock.usHeight :
    //											      xBlock.usAngle;
    //
    //			uint16_t usBlockSum = xBlock++;
    //			if (usChecksum == usBlockSum)
    //			{
    //				vRecvBlocks[ulBlockCount++] = xBlock;
    //			}
    //			else
    //			{
    //				u0_dbg_printf("usChecksum error!\t"
    //							  "Checksum %d\tBlocksum: %d\n",
    //							   usChecksum, usBlockSum);
    //			}
    //			eState = (ulBlockCount == ulMaxBlocks) ? DONE : READ_NEXT;
    //		}

    //        __inline void vStateReadNext()
    //        {
    //            uint16_t usRecv = usGetShort();
    //            switch (usRecv)
    //            {
    //                case 0xaa55:
    //                {
    //                    eBlockType = NORMAL;
    //                    eState = READ_FIRST;
    //                    break;
    //                }
    //                case 0xaa56:
    //                {
    //                	eBlockType = COLOR_CODED;
    //                    eState = READ_FIRST;
    //                    break;
    //                }
    //                default:
    //                {
    //                    eState = START;
    //                    break;
    //                }
    //            }
    //            usRecvLast = usRecv;
    //        }

    void vInitMapStr()
    {
        xStringMap[START] = std::string("START");
        xStringMap[READ_FIRST] = std::string("READ_FIRST");
        xStringMap[READ_BLOCK] = std::string("READ_BLOCK");
        xStringMap[READ_NEXT] = std::string("READ_NEXT");
        xStringMap[DONE] = std::string("DONE");
    }

private:
    std::vector<pixy::Block_t> xBlocks;
    std::map<State_t, std::string> xStringMap;
    uint32_t ulMaxBlocks;
    uint32_t ulBlockCount;
    uint16_t usChecksum;
    uint16_t usRecvLast;
};

} // namespace pixy
} // namespace team9

#endif
