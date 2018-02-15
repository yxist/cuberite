
// DropperEntity.cpp

// Implements the cRtopperEntity class representing a Dropper block entity

#include "Globals.h"
#include "DropperEntity.h"
#include "../Chunk.h"
#include "ChestEntity.h"
#include "FurnaceEntity.h"




cDropperEntity::cDropperEntity(BLOCKTYPE a_BlockType, NIBBLETYPE a_BlockMeta, int a_BlockX, int a_BlockY, int a_BlockZ, cWorld * a_World):
	Super(a_BlockType, a_BlockMeta, a_BlockX, a_BlockY, a_BlockZ, a_World)
{
	ASSERT(a_BlockType == E_BLOCK_DROPPER);
}



bool cDropperEntity::GetOutputBlockPos(NIBBLETYPE a_BlockMeta, int & a_OutputX, int & a_OutputY, int & a_OutputZ)
{
    a_OutputX = m_PosX;
    a_OutputY = m_PosY;
    a_OutputZ = m_PosZ;
    switch (a_BlockMeta & E_META_DROPSPENSER_FACING_MASK)
    {
        case E_META_DROPSPENSER_FACING_XM: a_OutputX--; return true;
        case E_META_DROPSPENSER_FACING_XP: a_OutputX++; return true;
        case E_META_DROPSPENSER_FACING_YM: a_OutputY--; return true;
        case E_META_DROPSPENSER_FACING_YP: a_OutputY++; return true;
        case E_META_DROPSPENSER_FACING_ZM: a_OutputZ--; return true;
        case E_META_DROPSPENSER_FACING_ZP: a_OutputZ++; return true;
        default:
        {
            // Not attached
            return false;
        }
    }
}





void cDropperEntity::DropSpenseFromSlot(cChunk & a_Chunk, int a_SlotNum)
{
    // Get the coords of the block where to output items:
    int OutX, OutY, OutZ;
    NIBBLETYPE Meta = a_Chunk.GetMeta(m_RelX, m_PosY, m_RelZ);
    if (!GetOutputBlockPos(Meta, OutX, OutY, OutZ))
    {
        // Not attached to another container
	    DropFromSlot(a_Chunk, a_SlotNum);
        return;
    }
    if (OutY < 0)
    {
        // Not attached to another container
	    DropFromSlot(a_Chunk, a_SlotNum);
        return;
    }
    // Convert coords to relative:
    int OutRelX = OutX - a_Chunk.GetPosX() * cChunkDef::Width;
    int OutRelZ = OutZ - a_Chunk.GetPosZ() * cChunkDef::Width;
    cChunk * DestChunk = a_Chunk.GetRelNeighborChunkAdjustCoords(OutRelX, OutRelZ);
    if (DestChunk == nullptr)
    {
        // The destination chunk has been unloaded, don't tick
	    DropFromSlot(a_Chunk, a_SlotNum);
        return;
    }

    // Call proper moving function, based on the blocktype present at the coords:
    switch (DestChunk->GetBlock(OutRelX, OutY, OutRelZ))
    {
        case E_BLOCK_TRAPPED_CHEST:
        case E_BLOCK_CHEST:
        {
            // Chests have special handling because of double-chests
            MoveItemsToChest(*DestChunk, OutX, OutY, OutZ);
            break;
        }
        case E_BLOCK_LIT_FURNACE:
        case E_BLOCK_FURNACE:
        {
            // Furnaces have special handling because of the direction-to-slot relation
            MoveItemsToFurnace(*DestChunk, OutX, OutY, OutZ, Meta);
            break;
        }
        case E_BLOCK_DISPENSER:
        case E_BLOCK_DROPPER:
        case E_BLOCK_HOPPER:
        {
            cBlockEntityWithItems * BlockEntity = static_cast<cBlockEntityWithItems *>(DestChunk->GetBlockEntity(OutX, OutY, OutZ));
            if (BlockEntity == nullptr)
            {
                LOGWARNING("%s: A block entity was not found where expected at {%d, %d, %d}", __FUNCTION__, OutX, OutY, OutZ);
                return;
            }
            MoveItemsToGrid(*BlockEntity);
            break;
        }
        default:
        {
	        DropFromSlot(a_Chunk, a_SlotNum);
        }
    }
}





bool cDropperEntity::MoveItemsToChest(cChunk & a_Chunk, int a_BlockX, int a_BlockY, int a_BlockZ)
{
    // Try the chest directly connected to the hopper:
    cChestEntity * ConnectedChest = static_cast<cChestEntity *>(a_Chunk.GetBlockEntity(a_BlockX, a_BlockY, a_BlockZ));
    if (ConnectedChest == nullptr)
    {
        LOGWARNING("%s: A chest entity was not found where expected, at {%d, %d, %d}", __FUNCTION__, a_BlockX, a_BlockY, a_BlockZ);
        return false;
    }
    if (MoveItemsToGrid(*ConnectedChest))
    {
        // Chest block directly connected was not full
        return true;
    }

    // Check if the chest is a double-chest (chest block directly connected was full), if so, try to move into the other half:
    static const struct
    {
        int x, z;
    }
    Coords [] =
    {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    } ;
    int RelX = a_BlockX - a_Chunk.GetPosX() * cChunkDef::Width;
    int RelZ = a_BlockZ - a_Chunk.GetPosZ() * cChunkDef::Width;
    for (size_t i = 0; i < ARRAYCOUNT(Coords); i++)
    {
        int x = RelX + Coords[i].x;
        int z = RelZ + Coords[i].z;
        cChunk * Neighbor = a_Chunk.GetRelNeighborChunkAdjustCoords(x, z);
        if (Neighbor == nullptr)
        {
            continue;
        }

        BLOCKTYPE Block = Neighbor->GetBlock(x, a_BlockY, z);
        if (Block != ConnectedChest->GetBlockType())
        {
            // Not the same kind of chest
            continue;
        }

        cChestEntity * Chest = static_cast<cChestEntity *>(Neighbor->GetBlockEntity(a_BlockX + Coords[i].x, a_BlockY, a_BlockZ + Coords[i].z));
        if (Chest == nullptr)
        {
            LOGWARNING("%s: A chest entity was not found where expected, at {%d, %d, %d} (%d, %d)", __FUNCTION__, a_BlockX + Coords[i].x, a_BlockY, a_BlockZ + Coords[i].z, x, z);
            continue;
        }
        if (MoveItemsToGrid(*Chest))
        {
            return true;
        }
        return false;
    }

    // The chest was single and nothing could be moved
    return false;
}





bool cDropperEntity::MoveItemsToFurnace(cChunk & a_Chunk, int a_BlockX, int a_BlockY, int a_BlockZ, NIBBLETYPE a_HopperMeta)
{
    cFurnaceEntity * Furnace = static_cast<cFurnaceEntity *>(a_Chunk.GetBlockEntity(a_BlockX, a_BlockY, a_BlockZ));
    if (a_HopperMeta == E_META_HOPPER_FACING_YM)
    {
        // Feed the input slot of the furnace
        return MoveItemsToSlot(*Furnace, cFurnaceEntity::fsInput);
    }
    else
    {
        // Feed the fuel slot of the furnace
        return MoveItemsToSlot(*Furnace, cFurnaceEntity::fsFuel);
    }
}





bool cDropperEntity::MoveItemsToGrid(cBlockEntityWithItems & a_Entity)
{
    // Iterate through our slots, try to move from each one:
    int NumSlots = a_Entity.GetContents().GetNumSlots();
    for (int i = 0; i < NumSlots; i++)
    {
        if (MoveItemsToSlot(a_Entity, i))
        {
            return true;
        }
    }
    return false;
}





bool cDropperEntity::MoveItemsToSlot(cBlockEntityWithItems & a_Entity, int a_DstSlotNum)
{
    cItemGrid & Grid = a_Entity.GetContents();
    if (Grid.IsSlotEmpty(a_DstSlotNum))
    {
        // The slot is empty, move the first non-empty slot from our contents:
        for (int i = 0; i < ContentsWidth * ContentsHeight; i++)
        {
            if (!m_Contents.IsSlotEmpty(i))
            {
                Grid.SetSlot(a_DstSlotNum, m_Contents.GetSlot(i).CopyOne());
                m_Contents.ChangeSlotCount(i, -1);
                return true;
            }
        }
        return false;
    }
    else
    {
        // The slot is taken, try to top it up:
        const cItem & DestSlot = Grid.GetSlot(a_DstSlotNum);
        if (DestSlot.IsFullStack())
        {
            return false;
        }
        for (int i = 0; i < ContentsWidth * ContentsHeight; i++)
        {
            if (m_Contents.GetSlot(i).IsEqual(DestSlot))
            {
                Grid.ChangeSlotCount(a_DstSlotNum, 1);
                m_Contents.ChangeSlotCount(i, -1);
                return true;
            }
        }
        return false;
    }
}

