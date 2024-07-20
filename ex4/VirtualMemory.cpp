#include "PhysicalMemory.h"

#define SUCCESS 1;
#define FAILURE 0;
#define LEAF_DEPTH (TABLES_DEPTH-1)


int calcInitialWidth() {
    if (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH == 0) {
        return OFFSET_WIDTH;
    } else {
        return VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH;
    }
}

uint64_t getOffSet(uint64_t i, uint64_t virutalAddress) {
    int initialWidth = calcInitialWidth();
    int shiftAmount = VIRTUAL_ADDRESS_WIDTH - initialWidth - OFFSET_WIDTH * i;

    return (virutalAddress >> shiftAmount) & (PAGE_SIZE - 1);
}

uint64_t getPhysicalAddress(uint64_t pageInd) {
    word_t retAddr = 0;
    for (word_t depth = 0; depth < TABLES_DEPTH; depth++) {
        uint64_t tempAddr = retAddr * PAGE_SIZE + getOffSet(depth, pageInd << OFFSET_WIDTH);

        if(depth == LEAF_DEPTH){
            return (tempAddr);
        }
        PMread(tempAddr, &retAddr);
    }

    return 0;
}

void eraseFrame(uint64_t frameInd) {
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameInd * PAGE_SIZE + i, 0);
    }
}

void readLeaf(uint64_t addr1, uint64_t virtualAddress, word_t *value) {
    uint64_t offset = getOffSet(TABLES_DEPTH, virtualAddress);
    uint64_t pageFrame = addr1 * PAGE_SIZE + offset;
    PMread(pageFrame, value);
}


typedef struct SearchFrame {

    word_t wantedPage;
    word_t &maxPage;
    word_t &maxDist;
    word_t freeFrame;
    word_t evictPage;
    word_t evictRoute;

    SearchFrame(word_t &maxFrame, word_t &maxDistPageIndex, word_t wantedPage) :
            wantedPage(wantedPage),
            maxPage(maxFrame),
            maxDist(maxDistPageIndex),
            freeFrame(0) {

    }

    void setFreeFrame(word_t frame) {
        this->freeFrame = frame;
    }

    void setEvictPage(word_t frame) {
        this->evictPage = frame;
    }

    void setEvictRoute(word_t frame) {
        this->evictRoute = frame;
    }


    word_t returnFrame(bool isFinalLevel)  {
        if (this->freeFrame != 0) {
            return this->freeFrame;
        }

        if (this->maxPage < NUM_FRAMES - 1) {
            if (!isFinalLevel) {
                eraseFrame(this->maxPage + 1);
            }
            return this->maxPage + 1;
        }
        else {
            PMevict(evictPage, evictRoute);
            if (!isFinalLevel) {
                eraseFrame(evictPage);
            }
            PMwrite(getPhysicalAddress(this->maxDist), 0);
            return evictPage;
        }
    }

    void updateMaxFrame(word_t frame) {
        if (maxPage < frame) {
            maxPage = frame;
        }
    }
} SearchFrame;

void treeTraversal(word_t baseFrame, word_t route, word_t depth, SearchFrame *searchFrame, word_t previousFrame);

word_t pageDist(word_t firstPage, word_t secondPage) {
    word_t dist1 = firstPage - secondPage < 0 ? secondPage - firstPage : firstPage - secondPage;
    word_t dist2 = NUM_PAGES - dist1;

    return (dist2 < dist1) ? dist2 :dist1;
}

void checkDistances(word_t  baseFrame,word_t currPage, word_t &maxPage, SearchFrame* searchFrame) {
    if (pageDist(searchFrame->wantedPage, currPage) >
            pageDist(searchFrame->wantedPage, maxPage)) {
        maxPage = currPage;
        searchFrame->setEvictPage(baseFrame);
        searchFrame->setEvictRoute(currPage);
    }
}

void processNode(word_t baseFrame, word_t i, word_t route, word_t depth, SearchFrame *searchFrame, word_t previousFrame,
                 bool &routeExists) {
    word_t nextFrame = 0;
    auto frameToRead = baseFrame * PAGE_SIZE + i;

    PMread(frameToRead, &nextFrame);
    if (nextFrame != 0) {
        searchFrame->updateMaxFrame(nextFrame);

        treeTraversal(nextFrame, (route << OFFSET_WIDTH) + i, depth + 1, searchFrame, previousFrame);

        if (searchFrame->freeFrame != 0 && nextFrame == searchFrame->freeFrame) {
                PMwrite(baseFrame * PAGE_SIZE + i, 0);
        }
        else{
            routeExists = true;
        }

    }
}


void disconnectIfFreeFrame(word_t baseFrame, word_t prevFrame, bool routeExists, SearchFrame *searchFrame) {
    if (!routeExists && (baseFrame != prevFrame)) {
        searchFrame->setFreeFrame(baseFrame);
    }
}

void treeTraversal(word_t baseFrame, word_t route, word_t depth, SearchFrame *searchFrame, word_t previousFrame) {
    if (depth == TABLES_DEPTH) {
        checkDistances(baseFrame, route, searchFrame->maxDist, searchFrame);
        return;
    }

    bool routeExists = false;
    word_t i = 0;
    while (true) {
        processNode(baseFrame, i, route, depth, searchFrame, previousFrame, routeExists);

        if (searchFrame->freeFrame != 0) {
            return;
        }

        if (++i >= PAGE_SIZE) {
            break;
        }
    }

    disconnectIfFreeFrame(baseFrame, previousFrame, routeExists, searchFrame);
}


word_t findFreeAddr(word_t queriedAddr, word_t originAddr, bool isFinalLevel) {
    word_t maxFrame = 0;
    word_t maxDist = queriedAddr;

    auto searchFrame = SearchFrame{maxFrame, maxDist, queriedAddr};
    treeTraversal(0, 0, 0, &searchFrame, originAddr);

    word_t retAddr = searchFrame.returnFrame(isFinalLevel);

    return retAddr;
}


void writeLeaf(uint64_t addr1, uint64_t virtualAddress, word_t value) {
    uint64_t offset = getOffSet(TABLES_DEPTH, virtualAddress);
    uint64_t pageFrame = addr1 * PAGE_SIZE + offset;
    PMwrite(pageFrame, value);
}

void traverseAndAllocate(uint64_t virtualAddress, word_t &addr1) {
    int depth = 0;
    while (depth < TABLES_DEPTH) {
        uint64_t offset = getOffSet(depth, virtualAddress);
        uint64_t pageTableFrame = addr1 * PAGE_SIZE + offset;
        word_t  previousFrame = addr1;
        PMread(pageTableFrame, &addr1);

        if (addr1 == 0) {
            if(depth != LEAF_DEPTH){
                addr1 = word_t(findFreeAddr(word_t(virtualAddress >> OFFSET_WIDTH),
                                            previousFrame, false));
                PMwrite(pageTableFrame, addr1);
            }
            else {
                addr1 = word_t(findFreeAddr(word_t(virtualAddress >> OFFSET_WIDTH),
                                            previousFrame, true));
                PMwrite(pageTableFrame, addr1);
                PMrestore(addr1, virtualAddress >> OFFSET_WIDTH);
            }
        }

        depth++;
    }


}

void VMinitialize() {
    for (int i = 0; i < PAGE_SIZE; i++) {
        PMwrite(i, 0);
    }
}

int VMread(uint64_t virtualAddress, word_t *value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return FAILURE;
    }

    word_t addr1 = 0;

    traverseAndAllocate(virtualAddress, addr1);

    readLeaf(addr1, virtualAddress, value);
    return SUCCESS;
}

int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return FAILURE;
    }

    word_t addr1 = 0;

    traverseAndAllocate(virtualAddress, addr1);

    writeLeaf(addr1, virtualAddress, value);
    return SUCCESS;
}
