#include "PhysicalMemory.h"

int calcInitialWidth() {
    if (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH == 0) {
        return OFFSET_WIDTH;
    } else {
        return VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH;
    }
}

uint64_t getOffSet(uint64_t i, uint64_t virutalAddress) {
    int initialWidth = calcInitialWidth();
    if (i == 0) {
        return virutalAddress >> (VIRTUAL_ADDRESS_WIDTH - initialWidth);
    } else {
        virutalAddress <<= (initialWidth + OFFSET_WIDTH * (i - 1));

        virutalAddress >>= (VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH);

        virutalAddress &= (PAGE_SIZE - 1);

        return virutalAddress;
    }
}

void eraseFrame(uint64_t frameInd) {
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameInd * PAGE_SIZE + i, 0);
    }
}

typedef struct SearchFrame{

    word_t wantedPage;
    word_t* maxFrame;
    word_t * maxDistPageIndex;
    word_t freeFrame;

    SearchFrame(word_t * maxFrame,word_t * maxDistPageIndex, word_t wantedPage):
    wantedPage(wantedPage),
    maxFrame(maxFrame),
    maxDistPageIndex(maxDistPageIndex),
    freeFrame(0){

    }

    void setFreeFrame(word_t frame){
        this->freeFrame = frame;
    }

    static word_t evictFrame(uint64_t pageInd, bool isFinalLevel){
        word_t retAddr = 0;
        uint64_t frame = 0;
        uint64_t offset = 0;
        for (word_t i = 0; i < TABLES_DEPTH; i++) {
            frame = retAddr * PAGE_SIZE;
            offset = getOffSet(i, pageInd << OFFSET_WIDTH);
            PMread(frame + offset, &retAddr);
        }
        PMevict(retAddr, pageInd);
        if (!isFinalLevel) {
            eraseFrame(retAddr);
        }
        PMwrite(frame + offset, 0);
        return retAddr;
    }

    word_t returnFrame(bool isFinalLevel) const{
        if (this->freeFrame != 0) {
            return this->freeFrame;
        } else if (*this->maxFrame < NUM_FRAMES - 1) {
            if (!isFinalLevel) {
                eraseFrame(*this->maxFrame + 1);
            }
            return *this->maxFrame + 1;
        }
        return SearchFrame::evictFrame(*this->maxDistPageIndex, isFinalLevel);
    }

    void updateMaxFrame(word_t frame){
        if (*maxFrame < frame) {
            *maxFrame = frame;
        }
    }
};

word_t pageDist(word_t firstPage, word_t secondPage) {
    word_t dist1 = firstPage - secondPage < 0 ? secondPage - firstPage : firstPage - secondPage;
    word_t dist2 = NUM_PAGES - dist1;
    if (dist2 < dist1) {
        return dist2;
    }
    return dist1;
}

void findMaxDistance(word_t currPage, word_t& maxPage, word_t wantedPage) {
    if (pageDist(wantedPage, currPage) > pageDist(wantedPage, maxPage)) {
        maxPage = currPage;
    }
}

void treeTraversal(word_t baseFrame, word_t route, word_t depth, SearchFrame* searchFrame, word_t prevFrame) {

    if (depth == TABLES_DEPTH) {
        findMaxDistance(route, *searchFrame->maxDistPageIndex, searchFrame->wantedPage);
        return;
    }

    bool routeExists = false;
    word_t i = 0;
    while (true) {
        word_t nextFrame = 0;
        auto frameToRead = baseFrame * PAGE_SIZE + i;

        PMread(frameToRead, &nextFrame);
        if (nextFrame != 0) {
            searchFrame->updateMaxFrame(nextFrame);

            treeTraversal(nextFrame, (route << OFFSET_WIDTH) + i,depth + 1,
                                              searchFrame, prevFrame);

            if (searchFrame->freeFrame != 0) {
                if (nextFrame == searchFrame->freeFrame) {
                    PMwrite(baseFrame * PAGE_SIZE + i, 0); // disconnect curr from next
                }
                return;
            }
            routeExists = true;
        }
        if(++i >= PAGE_SIZE){
            break;
        }
    }
    if (!routeExists && (baseFrame != prevFrame)) {
         searchFrame->setFreeFrame(baseFrame);
         return;
    }
}

word_t findFreeAddr(word_t queriedPageIndex, word_t originFrame, bool isFinalLevel) {
    word_t maxFrame = 0;
    word_t maxDistPageIndex = queriedPageIndex;

    auto* searchFrame = new SearchFrame(&maxFrame, &maxDistPageIndex, queriedPageIndex);
    treeTraversal(0, 0, 0, searchFrame, originFrame);

    return searchFrame->returnFrame(isFinalLevel);
}

void readLeaf(uint64_t addr1, uint64_t virtualAddress, word_t *value) {
    uint64_t offset = getOffSet(TABLES_DEPTH, virtualAddress);
    uint64_t pageFrame = addr1 * PAGE_SIZE + offset;
    PMread(pageFrame, value);
}

void writeLeaf(uint64_t addr1, uint64_t virtualAddress, word_t value) {
    uint64_t offset = getOffSet(TABLES_DEPTH, virtualAddress);
    uint64_t pageFrame = addr1 * PAGE_SIZE + offset;
    PMwrite(pageFrame, value);
}

/*
 * Initialize the virtual memory.
 */
void VMinitialize() {
    for (int i = 0; i < PAGE_SIZE; i++) {
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
int VMread(uint64_t virtualAddress, word_t *value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }

    word_t addr1 = 0;
    word_t pageTableFrame = 0;
    word_t prevFrame = 0;
    bool needToRestore = false;

    for (int d = 0; d < TABLES_DEPTH; d++) {
        uint64_t offset = getOffSet(d, virtualAddress);
        pageTableFrame = addr1 * PAGE_SIZE + offset;
        prevFrame = addr1;
        PMread(pageTableFrame, &addr1);

        if (addr1 == 0) {
            auto addr2 = word_t(findFreeAddr(word_t(virtualAddress >> OFFSET_WIDTH),
                                             prevFrame, d == TABLES_DEPTH - 1));
            PMwrite(pageTableFrame, addr2);
            needToRestore = true;
            addr1 = addr2;
        }
    }

    if (needToRestore) {
        PMrestore(addr1, virtualAddress >> OFFSET_WIDTH);
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
int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    word_t addr1 = 0;
    word_t prevFrame;
    bool needToRestore = false;
    for (int d = 0; d < TABLES_DEPTH; d++) {
        uint64_t offset = getOffSet(d, virtualAddress);
        uint64_t pageTableFrame = addr1 * PAGE_SIZE + offset;
        prevFrame = addr1;
        PMread(pageTableFrame, &addr1);

        if (addr1 == 0) {
            auto newAddr = word_t(findFreeAddr(word_t(virtualAddress >> OFFSET_WIDTH),
                                               prevFrame, d == TABLES_DEPTH - 1));
            int addr2 = newAddr;
            PMwrite(pageTableFrame, addr2);
            needToRestore = true;
            addr1 = addr2;
        }
    }

    if (needToRestore) {
        PMrestore(addr1, virtualAddress >> OFFSET_WIDTH);
    }

    writeLeaf(addr1, virtualAddress, value);
    return 1;
}