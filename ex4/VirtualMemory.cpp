#include "PhysicalMemory.h"

uint64_t getOffSet(int depth, uint64_t virtualAddress){

    uint64_t firstOffSetWidth = depth==0 ? 0:VIRTUAL_ADDRESS_WIDTH%OFFSET_WIDTH;
    uint64_t otherOffSetWidth = depth==0 ? 0:OFFSET_WIDTH * (depth-1);

    uint64_t moveTheLeftBits = virtualAddress << (otherOffSetWidth + firstOffSetWidth);
    uint64_t backToCorrectPosition = moveTheLeftBits >> (otherOffSetWidth + firstOffSetWidth);
    uint64_t moveBitsToTheRight = backToCorrectPosition >> (OFFSET_WIDTH * (TABLES_DEPTH - depth - 1));
    return moveBitsToTheRight;
}

word_t  getNewDistance(word_t frame, word_t pageFrame){
    word_t absFrame = (pageFrame-frame > 0)? pageFrame-frame : frame-pageFrame;
    word_t num1 = word_t(NUM_PAGES) - absFrame;
    word_t num2 = absFrame;
    return (num1 > num2) ? num2: num1;
}


typedef struct FreeFrame{
    word_t emptyTableFrame;
    word_t unusedFrame;
    word_t evictedFrame;
    word_t maxDistanceFrame;

    FreeFrame():
        emptyTableFrame(0),
        unusedFrame(0),
        evictedFrame(0),
        maxDistanceFrame(0){}

    void updateMaxUnusedFrame(word_t frame){
        if(frame > this->unusedFrame){
            this->unusedFrame = frame;
        }
    }

    void updateMaxDistanceFrame(word_t frame, word_t pageFrame){
        word_t newDist = getNewDistance(frame, pageFrame);
        if(newDist > this->maxDistanceFrame){
            this->maxDistanceFrame = newDist;
            this->evictedFrame = frame;
        }
    }

    bool disconnectFrame(uint64_t frame, uint64_t frameToDisconnect) const{
        if(this->emptyTableFrame == frame){
            PMwrite(frameToDisconnect, 0);
            return true;
        }
        return false;
    }

    word_t evictFrame() const{
        word_t addr1 = 0;
        uint64_t pageTableFrame = 0;

        for(word_t d=0; d<TABLES_DEPTH; d++){
            uint64_t offset = getOffSet(d, this->evictedFrame);
            pageTableFrame = addr1 * PAGE_SIZE + offset;

            PMread(pageTableFrame, &addr1);
        }

        PMevict(addr1, this->evictedFrame);
        PMwrite(pageTableFrame, 0);

        return addr1;
    }

} FreeFrame;

word_t frameReturnFunc(FreeFrame* newFrame){
    if(newFrame->emptyTableFrame != 0){
        return newFrame->emptyTableFrame;
    }

    if(newFrame->unusedFrame + 1 < NUM_FRAMES){
        return newFrame->unusedFrame + 1;
    }

    return newFrame->evictFrame();
}

void traverseTree(word_t baseFrame, word_t currRoute, word_t pageFrame, FreeFrame* newFrame, word_t depth){

    newFrame->updateMaxUnusedFrame(baseFrame);

    if(depth == TABLES_DEPTH){
        newFrame->updateMaxDistanceFrame(currRoute, pageFrame);
        return;
    }

    for(int i = 0; i < PAGE_SIZE; i++){

        word_t checkNextFrame = 0;
        auto frameToRead = baseFrame*PAGE_SIZE + i;
        word_t updatedRoute = (currRoute << OFFSET_WIDTH) + i;

        PMread(frameToRead, &checkNextFrame);

        if(checkNextFrame != 0){
            traverseTree(checkNextFrame, updatedRoute, pageFrame, newFrame, depth+1);
            if(newFrame->disconnectFrame(checkNextFrame, frameToRead)){
                return;
            }
        }
    }

    newFrame->emptyTableFrame = baseFrame;
}

word_t getAddrForFrame(word_t pagePath, word_t depth){
    auto* newFrame = new FreeFrame();
    word_t startingFrame = 0;
    word_t startingRoute = 0;

    traverseTree(startingFrame, startingRoute, pagePath, newFrame, depth);

    return frameReturnFunc(newFrame);
}

void readLeaf(uint64_t addr1, uint64_t virtualAddress, word_t* value){
    uint64_t offset = getOffSet(TABLES_DEPTH, virtualAddress);
    uint64_t pageFrame = addr1 * PAGE_SIZE + offset;
    PMread(pageFrame, value);
}

void writeLeaf(uint64_t addr1, uint64_t virtualAddress, word_t value){
    uint64_t offset = getOffSet(TABLES_DEPTH, virtualAddress);
    uint64_t pageFrame = addr1 * PAGE_SIZE + offset;
    PMwrite(pageFrame, value);
}

/*
 * Initialize the virtual memory.
 */
void VMinitialize(){
    for(int i = 0; i < PAGE_SIZE; i++){
        PMwrite(i, 0);
    }
}


/* Reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t* value){
    if(virtualAddress > VIRTUAL_ADDRESS_WIDTH){
        return 0;
    }

    word_t addr1 = 0;
    word_t pageTableFrame = 0;
    word_t virtualAddressWithoutOffset = word_t(virtualAddress >> OFFSET_WIDTH);
    bool needToRestore = false;

    for(int d = 0; d < TABLES_DEPTH; d++){
        uint64_t offset = getOffSet(d, virtualAddress);
        pageTableFrame = addr1 * PAGE_SIZE + offset;

        PMread(pageTableFrame, &addr1);

        if(addr1 == 0){
            addr1 = getAddrForFrame(virtualAddressWithoutOffset, d);
            
            PMwrite(pageTableFrame, addr1);
            needToRestore = true;
        }
    }

    if(needToRestore){
        PMrestore(addr1, virtualAddressWithoutOffset);
    }

    readLeaf(addr1, virtualAddress, value);
    return 1;
}


/* Writes a word to the given virtual address.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value){
    if(virtualAddress > VIRTUAL_ADDRESS_WIDTH){
        return 0;
    }

    word_t addr1 = 0;
    uint64_t pageTableFrame = 0;
    uint64_t virtualAddressWithoutOffset = virtualAddress >> OFFSET_WIDTH;
    bool needToRestore = false;

    for(int d = 0; d < TABLES_DEPTH; d++){
        uint64_t offset = getOffSet(d, virtualAddress);
        pageTableFrame = addr1 * PAGE_SIZE + offset;

        PMread(pageTableFrame, &addr1);

        if(addr1 == 0){
            addr1 = getAddrForFrame(virtualAddressWithoutOffset, d);
            
            PMwrite(pageTableFrame, addr1);
            needToRestore = true;
        }
    }

    if(needToRestore){
        PMrestore(addr1, virtualAddressWithoutOffset);
    }

    writeLeaf(addr1, virtualAddress, value);
    return 1;
}