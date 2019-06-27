// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "alert.h"
#include "checkpoints.h"
#include "db.h"
#include "txdb.h"
#include "net.h"
#include "init.h"
#include "ui_interface.h"
#include "kernel.h"
#include "zerocoin/Zerocoin.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <limits>

using namespace std;
using namespace boost;

//
// Global state
//
bool ForceTransitionOnNextNodeActivity=false;

CCriticalSection cs_setpwalletRegistered;
set<CWallet*> setpwalletRegistered;

CCriticalSection cs_main;

CCriticalSection cs_IO;
CCriticalSection cs_Accept;


CTxMemPool mempool;
unsigned int nTransactionsUpdated = 0;

map<uint256, CBlockIndex*> mapBlockIndex;
set<pair<COutPoint, unsigned int> > setStakeSeen;
libzerocoin::Params* ZCParams;


CBigNum GetWorkLimit(int nHeight = -1)
{
    if (nHeight == -1)
        nHeight = pindexBest?pindexBest->nHeight:0;

    if (fTestNet && nHeight <= LAST_POW_BLOCK)
        return CBigNum(~uint256(0) >> 10);
    return CBigNum(~uint256(0) >> 20); // "standard" scrypt target limit for proof of work, results with 0,000244140625 proof-of-work difficulty
}


int64_t GetTargetSpacing(int nHeight)
{
    if (nHeight == -1)
        nHeight = pindexBest->nHeight;

    if (fTestNet && nHeight < 950000)
    {
        if (nHeight > 136167)
        {
            return 30; // 30 seconds
        }
        else if (nHeight >= 50)
        {
            return 1; // 1 second
        }
        else
        {
            return 4; // 4 seconds
        }
    }
    return 1 * 60; // 1 minute
}

int64_t GetTargetTimespan()
{
    if (fTestNet)
    {
        return 120; // Pandacoin: every 2 minutes.
    }
    return 4 * 60 * 60; // Pandacoin: every 4 hours
}

int64_t GetInterval()
{
    return GetTargetTimespan() / GetTargetSpacing();
}


unsigned int nStakeMinAge = 8 * 60 * 60; // 8 hours
unsigned int nStakeMaxAge = 2592000; // 30 days
unsigned int nModifierInterval = 10 * 60; // time to elapse before new modifier is computed

int nCoinbaseMaturity = 30;
int nCoinstakeMaturity = 500;
CBlockIndex* pindexGenesisBlock = NULL;
int nBestHeight = -1;

uint256 nBestChainTrust = 0;
uint256 nBestInvalidTrust = 0;

uint256 hashBestChain = 0;
CBlockIndex* pindexBest = NULL;
int64_t nTimeBestReceived = 0;

CMedianFilter<int> cPeerBlockCounts(5, 0); // Amount of blocks that other nodes claim to have

map<uint256, CBlock*> mapOrphanBlocks;
multimap<uint256, CBlock*> mapOrphanBlocksByPrev;
set<pair<COutPoint, unsigned int> > setStakeSeenOrphan;

map<uint256, CTransaction> mapOrphanTransactions;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev;

// Constant stuff for coinbase transactions we create:
CScript COINBASE_FLAGS;

const string strMessageMagic = "Pandacoin Signed Message:\n";

// Settings
int64_t nTransactionFee = 0;
int64_t nReserveBalance = 0;
int64_t nMinimumInputValue = 0;

extern enum Checkpoints::CPMode CheckpointsMode;


LoadState currentLoadState = LoadState_Begin;
LoadState prevLoadState = LoadState_Begin;
ClientMode currentClientMode = ClientHybrid;
int epochCheckpointDepth = 0;


// Helper function to request headers
void PushGetHeadersFromHash(CNode* pnode, uint256 from, uint256 to)
{
    std::vector<uint256> vHaveIn;
    vHaveIn.push_back(from);
    pnode->PushMessage("getheaders", CBlockLocator(vHaveIn), (uint256)to);
}

// Helper function to allocate 'ranges' to a node and jump start 'pumping' of the range header data.
void PushGetHeaderRange(CNode* pnode)
{
    LOCK2(cs_vNodes, pnode->cs_alterSyncRanges);
    if(!pnode->RangesToSync.empty() && pnode->syncFrom == uint256(0) && !pnode->isSyncing)
    {
        pnode->syncFrom = pnode->RangesToSync.back().first;
        pnode->syncTo = pnode->RangesToSync.back().second;
        pnode->isSyncing = true;
        pnode->RangesToSync.pop_back();
        if(fDebugNetRanges)
            printf("[%s] Start range header sync %s -> %s - Ranges remaining [%d]\n", pnode->addrName.c_str(), pnode->syncFrom.ToString().c_str(), pnode->syncTo.ToString().c_str(), pnode->NumRangesToSync);

        pnode->nLastRecvHeader = GetTime();
        PushGetHeadersFromHash(pnode, pnode->syncFrom, pnode->syncTo);
    }
}

//Helper function to request blocks
void PushGetBlocksFromHash(CNode* pnode, uint256 from, uint256 to)
{
    std::vector<uint256> vHaveIn;
    vHaveIn.push_back(from);
    pnode->PushMessage("getblocks", CBlockLocator(vHaveIn), (uint256)to);
}

// Helper function to allocate 'ranges' to a node and jump start 'pumping' of the range block data.
void PushGetBlockRange(CNode* pnode)
{
    LOCK2(cs_vNodes, pnode->cs_alterSyncRanges);
    if(!pnode->RangesToSync.empty() && pnode->syncFrom == uint256(0) && !pnode->isSyncing)
    {
        pnode->syncFrom = pnode->RangesToSync.back().first;
        pnode->syncTo = pnode->RangesToSync.back().second;
        pnode->isSyncing = true;
        pnode->RangesToSync.pop_back();
        if(fDebugNetRanges)
            printf("[%s] Start range sync %s:%d [%s] -> %s:%d [%s] - Ranges remaining [%d]\n", pnode->addrName.c_str(), pnode->syncFrom.ToString().c_str(), mapBlockIndex.count(pnode->syncFrom)?mapBlockIndex[pnode->syncFrom]->nHeight:-1, mapBlockIndex[pnode->syncFrom]->IsHeaderOnly()?"header":"block", pnode->syncTo.ToString().c_str(), mapBlockIndex.count(pnode->syncTo)?mapBlockIndex[pnode->syncTo]->nHeight:-1, mapBlockIndex[pnode->syncFrom]->IsHeaderOnly()?"header":"block", pnode->NumRangesToSync);

        pnode->nLastRecvBlock = GetTime();

        CBlockIndex* pIndex = mapBlockIndex[pnode->syncFrom];
        bool askedForAny = false;
        while (pIndex)
        {
            if (pIndex->IsHeaderOnly())
            {
                pnode->AskFor(CInv(MSG_BLOCK, pIndex->GetBlockHash()));
                askedForAny = true;
            }
            if (pIndex->GetBlockHash() == pnode->syncTo)
                break;
            pIndex = pIndex->pnext;
        }

        if (!askedForAny)
        {
            pnode->NumRangesToSync--;
            if(fDebugNetRanges)
                printf("[%s] End range sync %s:%d [%s] -> %s:%d [%s] - Ranges remaining [%d]\n", pnode->addrName.c_str(), pnode->syncFrom.ToString().c_str(), mapBlockIndex.count(pnode->syncFrom)?mapBlockIndex[pnode->syncFrom]->nHeight:-1, mapBlockIndex[pnode->syncFrom]->IsHeaderOnly()?"header":"block", pnode->syncTo.ToString().c_str(), mapBlockIndex.count(pnode->syncFrom)?mapBlockIndex[pnode->syncTo]->nHeight:-1, mapBlockIndex[pnode->syncFrom]->IsHeaderOnly()?"header":"block", pnode->NumRangesToSync);
            pnode->syncFrom = uint256(0);
            pnode->syncTo = uint256(0);
            pnode->isSyncing = false;
        }
    }
}



void CreateBlockRanges(CNode* pfrom, CBlockIndex* pIndexBeginInclusive, CBlockIndex* pIndexEndInclusive)
{
    pfrom->RangesToSync.clear();
    pfrom->NumRangesToSync=0;

    // Start from first header only block.
    while (pIndexBeginInclusive && !pIndexBeginInclusive->IsHeaderOnly())
        pIndexBeginInclusive = pIndexBeginInclusive->pnext;

    if (!pIndexBeginInclusive)
        return;

    LOCK2(cs_vNodes, pfrom->cs_alterSyncRanges);

    // The start block (inclusive) that we want the range to fetch from - when pushing the range we use pIndexBeginInclusive->pprev to get the actual range because ranges are start exclusive but end inclusive.
    CBlockIndex* pIndex = pIndexBeginInclusive;

    int rangeSize = 0;
    while (true)
    {
        if (!pIndex->pnext || pIndex == pIndexEndInclusive)
        {
            if (pIndexBeginInclusive != pIndexEndInclusive || pIndexEndInclusive->IsHeaderOnly())
            {
                pfrom->RangesToSync.push_front(std::make_pair(pIndexBeginInclusive->pprev?pIndexBeginInclusive->pprev->GetBlockHash():pIndexBeginInclusive->GetBlockHash(), pIndex->pnext?pIndex->pnext->GetBlockHash():pIndex->GetBlockHash()));
                pfrom->NumRangesToSync++;
            }
            break;
        }

        if (!pIndex->IsHeaderOnly() || rangeSize >= 2500)
        {
            if (pIndex->IsHeaderOnly())
            {
                pfrom->RangesToSync.push_front(std::make_pair(pIndexBeginInclusive->pprev?pIndexBeginInclusive->pprev->GetBlockHash():pIndexBeginInclusive->GetBlockHash(), pIndex->GetBlockHash()));
                pIndexBeginInclusive = pIndex = pIndex->pnext;
            }
            else
            {
                // Skip fetching the block that we already have.
                pfrom->RangesToSync.push_front(std::make_pair(pIndexBeginInclusive->pprev?pIndexBeginInclusive->pprev->GetBlockHash():pIndexBeginInclusive->GetBlockHash(), pIndex->pprev->GetBlockHash()));
                pIndexBeginInclusive = pIndex->pnext;
            }

            pfrom->NumRangesToSync++;
            rangeSize=0;

            // Skip past subsequent loaded blocks.
            bool anySkipped = false;
            while (pIndexBeginInclusive && !pIndexBeginInclusive->IsHeaderOnly())
            {
                pIndexBeginInclusive = pIndexBeginInclusive->pnext;
                anySkipped = true;
            }

            if (!pIndexBeginInclusive)
                break;

            if (anySkipped)
                pIndex = pIndexBeginInclusive;
        }
        pIndex = pIndex->pnext;
        if (!pIndex)
            break;
        ++rangeSize;
    }
    printf("Created [%d] ranges to sync.\n",pfrom->NumRangesToSync);

    //Distribute block fetching amongst as many other nodes as possible.
    // Randomise so that we don't keep sending to the same node, if e.g. the first node in the array is bad, give all our nodes a fair chance.
    vector<CNode*> vNodesCopy = vNodes;
    std::random_shuffle(vNodesCopy.begin(), vNodesCopy.end());
    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        pnode->syncFrom = uint256(0);
        pnode->syncTo = uint256(0);
        pnode->isSyncing = false;
        PushGetBlockRange(pnode);
    }
}

void CheckBlockRangeEnds(CNode* pfrom)
{
    LOCK2(cs_vNodes, pfrom->cs_alterSyncRanges);


    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(pnode->isSyncing)
        {
            // Test for end of range.
            bool endRange=false;
            CBlockIndex* pBlockIndex = mapBlockIndex[pnode->syncTo];
            while (pBlockIndex->pprev && !pBlockIndex->IsHeaderOnly())
            {
                if (pBlockIndex->GetBlockHash() == pnode->syncFrom)
                {
                    endRange = true;
                    break;
                }
                pBlockIndex = pBlockIndex->pprev;
            }

            // Found end of range.
            if (endRange)
            {
                // Clear range values.
                pnode->NumRangesToSync--;
                if(fDebugNetRanges)
                    printf("[%s] End range sync %s:%d [%s] -> %s:%d [%s] - Ranges remaining [%d]\n", pnode->addrName.c_str(), pnode->syncFrom.ToString().c_str(), mapBlockIndex.count(pnode->syncFrom)?mapBlockIndex[pnode->syncFrom]->nHeight:-1, mapBlockIndex[pnode->syncFrom]->IsHeaderOnly()?"header":"block", pnode->syncTo.ToString().c_str(), mapBlockIndex.count(pnode->syncFrom)?mapBlockIndex[pnode->syncTo]->nHeight:-1, mapBlockIndex[pnode->syncFrom]->IsHeaderOnly()?"header":"block", pnode->NumRangesToSync);
                pnode->syncFrom = uint256(0);
                pnode->syncTo = uint256(0);
                pnode->isSyncing = false;
            }
        }
    }

    // Start on next range if we are waiting for any more ranges.
    // NB! We can actually detect end of range inside PushGetBlockRange in some instances.
    vector<CNode*> vNodesCopy = vNodes;
    std::random_shuffle(vNodesCopy.begin(), vNodesCopy.end());
    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        PushGetBlockRange(pnode);
    }

    // No ranges left
    if (pfrom->NumRangesToSync <= 0)
    {
        // Create ranges again for any blocks we may have missed.
        pfrom->RangesToSync.clear();
        if (currentLoadState == LoadState_SyncBlocksFromEpoch)
        {
            CreateBlockRanges(pfrom, mapBlockIndex[Checkpoints::GetEpochHash(firstWalletTxTime)], pindexBest);
        }
        else if(currentLoadState == LoadState_SyncAllBlocks)
        {
            CreateBlockRanges(pfrom, mapBlockIndex[hashGenesisBlock], pindexBest);
        }

        // All blocks have fetched now transition to next phase.
        if (pfrom->NumRangesToSync <= 0)
        {
            // Go into 'listen' mode for new blocks (all blocks) or sync 'all headers' (blocks from epoch).
            TransitionLoadState(pfrom);
        }
    }
}

// Helper function to handle distributing work to nodes e.g. when a new node connects during a sync, or when a node disconnects and its work needs to be passed on to other existing peers.
void PushWork(CNode* pfrom)
{
    // Other node disconnected while we were syncing headers, or we are a new node that is not yet doing any syncing
    if(currentLoadState == LoadState_SyncAllHeaders || currentLoadState == LoadState_SyncHeadersFromEpoch)
    {
        PushGetHeaderRange(pfrom);
    }
    // Other node disconnected while we were syncing blocks, or we are a new node that is not yet doing any syncing
    else if(currentLoadState == LoadState_SyncAllBlocks || currentLoadState == LoadState_SyncBlocksFromEpoch)
    {
        CheckBlockRangeEnds(pfrom);
    }
}

int numEpochTransactionsScanned = 0;
int numEpochTransactionsToScan = 0;
void VerifyEpochBlocks(void* parg)
{
    RenameThread("pandacoin-verify_epoch_blocks");

    FlushBlockFile();

    CBlockIndex* pEpochBlock = mapBlockIndex[Checkpoints::GetEpochHash(firstWalletTxTime)];
    while(pEpochBlock->nHeight != epochCheckpointDepth)
        pEpochBlock = pEpochBlock->pnext;
    pwalletMain->ScanForWalletTransactions(pEpochBlock, true, &numEpochTransactionsScanned, &numEpochTransactionsToScan);

    ForceTransitionOnNextNodeActivity = true;
}

bool abortVerifyAllBlocks = false;
bool denyIncomingBlocks = false;
CCriticalSection cs_VerifyAllBlocks;
void VerifyAllBlocks(void* parg)
{
    RenameThread("pandacoin-verify_all_blocks");

    {
        LOCK(cs_VerifyAllBlocks);

        printf("Start verify all blocks\n");

        FlushBlockFile();

        CTxDB txdb;
        numBlocksToVerify = pindexBest->nHeight;
        numBlocksVerified = 1;

        if (!txdb.TxnBeginTxPool())
            return;

        CBlockIndex* pIndex = mapBlockIndex[hashGenesisBlock];
        while(pIndex)
        {
            if(pIndex->IsHeaderOnly())
            {
                string strError = strprintf("Failed to load block %s\n", pIndex->GetBlockHash().ToString().c_str());
                ShowWarningAndResetBlockchain(strError);
                return;
            }
            pIndex = pIndex->pnext;
        }

        vector<pair<int64_t, uint256> > vSortedByTimestamp;

        int numNewVerified = 0;
        pIndex = pindexGenesisBlock;
        while(pIndex)
        {
            if ( currentLoadState == LoadState_Exiting || abortVerifyAllBlocks )
            {
                txdb.TxnEndTxPool();
                return;
            }

            numBlocksToVerify = pindexBest->nHeight;
            numBlocksVerified++;

            // Skip already verified blocks - except genesis we must always do this as we mark it verified even before it isn't (to indicate hybrid blockchain)
            if (!pIndex->IsVerified() || pIndex == pindexGenesisBlock)
            {
                numNewVerified++;

                CBlock block;
                block.ReadFromDisk(pIndex, true);
                block.ConnectBlock(txdb, pIndex, false, true);

                uint256 hashProof = 0;
                if (pIndex->IsProofOfStake())
                {
                    uint256 targetProofOfStake = 0;
                    if (!CheckProofOfStake(txdb, block.vtx[1], block.nTime, block.nBits, hashProof, targetProofOfStake))
                    {
                        string strError = strprintf("WARNING: VerifyAllBlocks(): check proof-of-stake failed for block %s\n", block.GetHash().ToString().c_str());
                        ShowWarningAndResetBlockchain(strError);
                        return;
                    }
                }
                if (pIndex->IsProofOfWork())
                {
                    hashProof = block.GetPoWHash();
                }
                pIndex->hashProof = hashProof;


                uint64_t nStakeModifier = 0;
                bool fGeneratedStakeModifier = false;
                if (!ComputeNextStakeModifier(pIndex->pprev, nStakeModifier, fGeneratedStakeModifier, vSortedByTimestamp))
                {
                    string strError = "ERROR computing stake modifier, staking will not work.\n";
                    ShowWarningAndResetBlockchain(strError);
                    return;
                }
                pIndex->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);

                pIndex->nStakeModifierChecksum = GetStakeModifierChecksum(pIndex);

                if(fDebug)
                    printf(">>><<<STAKEMODIFER - blockhash:%s stakemodifier:0x%016" PRIx64 " stakemodifierchecksum:%d\n", pIndex->GetBlockHash().ToString().c_str() ,nStakeModifier, pIndex->nStakeModifierChecksum);

                pIndex->SetVerified(true);

                //checkme: make sure this is necessary
                if (!txdb.WriteBlockIndex(pIndex->GetBlockHash(), CDiskBlockIndex(pIndex)))
                {
                    string strError = "Failed to write block index.\n";
                    ShowWarningAndResetBlockchain(strError);
                    return;
                }
            }
            pIndex = pIndex->pnext;
        }

        // Will always be at least 1 because of genesis block - we only want to run these expensive functions if there is something new...
        if(numNewVerified > 1)
        {
            LOCK(cs_Accept);

            if (!txdb.TxnEndTxPool())
            {
                string strError = "Failed to write transaction pool.\n";
                ShowWarningAndResetBlockchain(strError);
                return;
            }

            pwalletMain->ScanForWalletTransactions(pIndex, true);
            //pwalletMain->ReacceptWalletTransactions();
        }
    }

    {
        LOCK(cs_ChangeLoadState);
        currentLoadState = LoadState_AcceptingNewBlocks;
    }

    printf("Done verify all blocks\n");


}

void UpgradeToHybridBlockchain(CBlockIndex* pIndex)
{
    CTxDB txdb;
    txdb.TxnBegin();
    while(pIndex)
    {
        pIndex->SetVerified(true);
        txdb.WriteBlockIndex(pIndex->GetBlockHash(), CDiskBlockIndex(pIndex));
        pIndex = pIndex->pnext;
    }
    txdb.TxnCommit();
}

CCriticalSection cs_ChangeLoadState;
// Helper function to manage 'state machine' - transition between various load states as needed.
bool TransitionLoadState(CNode* pfrom)
{
    LOCK(cs_ChangeLoadState);
    switch(currentLoadState)
    {
        case LoadState_Exiting:
            break;
        case LoadState_Begin:
        {
            currentLoadState = LoadState_Connect;

            // Upgrade to hybrid blockchain if necessary
            if ( currentClientMode != ClientFull )
            {
                if (!pindexGenesisBlock->IsVerified())
                    UpgradeToHybridBlockchain(pindexGenesisBlock);
            }

            if (!NewThread(StartNode, NULL))
            {
                uiInterface.ThreadSafeMessageBox("Error: could not start node", _("Pandacoin"), CClientUIInterface::OK | CClientUIInterface::MODAL);
                return false;
            }
        }
        break;
        case LoadState_Connect:
        {
            epochCheckpointDepth = 0;
            if (currentClientMode == ClientFull)
            {
                // If there is an existing hybrid blockchain then we must delete it.
                if(!fNewBlockChain && pindexGenesisBlock->IsVerified())
                {
                    //fixme: delete blockchain here.
                }
                else
                {
                    //fixme: Get load state from file.. (In case of e.g. forced close during initial sync)
                    currentLoadState = LoadState_AcceptingNewBlocks;
                    // Jump start block synchronisation
                    pfrom->PushGetBlocks(pindexBest, uint256(0));
                }
            }
            else
            {
                // If new blockchain or existing non hybrid blockchain then mark it as a hybrid one.
                if( (fNewBlockChain && !( currentClientMode == ClientFull )) || (!fNewBlockChain && !pindexGenesisBlock->IsVerified()) )
                {
                    // Mark genesisblock as verified - this indicates to future loads that we are using a 'new' hybrid blockchain.
                    {
                        pindexGenesisBlock->SetVerified(true);
                        CTxDB txdb;
                        if (!txdb.TxnBegin())
                            return false;
                        txdb.WriteBlockIndex(pindexGenesisBlock->GetBlockHash(), CDiskBlockIndex(pindexGenesisBlock));
                        if (!txdb.TxnCommit())
                            return false;
                    }

                }
                // In all cases proceed with phase 1 - checkpoint loading...
                currentLoadState = LoadState_CheckPoint;
                Checkpoints::LoadCheckpoints(pfrom);

                // Already have them all?
                if ( Checkpoints::GetNumLoadedCheckpoints() == Checkpoints::GetNumCheckpoints() )
                {
                    TransitionLoadState(pfrom);
                }
            }
        }
        break;
        case LoadState_CheckPoint:
        {
            assert(currentClientMode != ClientFull);

            // Insert special 'placeholder' blocks so that we can form a proper chain without orphans
            Checkpoints::InsertPlaceHoldersBetweenCheckpoints();

            currentLoadState = LoadState_SyncHeadersFromEpoch;

            LOCK2(cs_vNodes, pfrom->cs_alterSyncRanges);
            CBlockIndex* pFetchRanges = pindexBest;
            pfrom->RangesToSync.clear();
            pfrom->RangesToSync.push_front(std::make_pair(pFetchRanges->pprev->GetBlockHash(), uint256(0)));
            pfrom->NumRangesToSync = 1;
            numSyncedHeaders = 0;
            while(true)
            {
                if (!pFetchRanges->pprev || pFetchRanges->GetBlockHash() == Checkpoints::GetEpochHash(firstWalletTxTime))
                    break;
                pFetchRanges = pFetchRanges->pprev;
                if(pFetchRanges->IsPlaceHolderBlock())
                {
                    uint256 from = pFetchRanges->pprev->GetBlockHash();
                    uint256 to = pFetchRanges->pnext->GetBlockHash();

                    pfrom->RangesToSync.push_front(std::make_pair(from, to));
                    pfrom->NumRangesToSync++;
                }
                else
                {
                    numSyncedHeaders++;
                }
            }

            if (pfrom->NumRangesToSync == 0)
            {
                TransitionLoadState(pfrom);
            }
            else
            {
                //Distribute header fetching amongst as many other nodes as possible.
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    pnode->syncFrom = uint256(0);
                    pnode->syncTo = uint256(0);
                    pnode->isSyncing = false;
                    PushGetHeaderRange(pnode);
                }
            }
        }
        break;
        case LoadState_SyncHeadersFromEpoch:
        {
            assert(currentClientMode != ClientFull);

            //fixme: (LIGHT) - Use filters to only get the blocks we need...
            currentLoadState = LoadState_SyncBlocksFromEpoch;

            numSyncedBlocks = 0;
            CBlockIndex* pEpochIndex = mapBlockIndex[Checkpoints::GetEpochHash(firstWalletTxTime)];
            CBlockIndex* pIndex = pindexGenesisBlock;
            while (pIndex && pIndex != pindexBest)
            {
                if (!pIndex->IsHeaderOnly())
                    numSyncedBlocks++;
                pIndex = pIndex->pnext;
            }

            CreateBlockRanges(pfrom, pEpochIndex, pindexBest);
            if (pfrom->NumRangesToSync == 0)
            {
                TransitionLoadState(pfrom);
            }
        }
        break;
        case LoadState_SyncBlocksFromEpoch:
        {
            assert(currentClientMode != ClientFull);

            numSyncedEpochBlocks = numSyncedBlocks;

            currentLoadState = LoadState_ScanningTransactionsFromEpoch;
            NewThread(VerifyEpochBlocks, NULL);
        }
        break;
        case LoadState_ScanningTransactionsFromEpoch:
        {
            if (currentClientMode == ClientLight)
            {
                currentLoadState = LoadState_AcceptingNewBlocks;
            }
            else
            {
                currentLoadState = LoadState_SyncAllHeaders;

                pfrom->syncFrom = uint256(0);
                pfrom->syncTo = uint256(0);
                pfrom->isSyncing = false;
                std::set<uint256>::const_iterator i = AdditionalBlocksToFetch.begin();
                for(; i != AdditionalBlocksToFetch.end(); i++)
                {
                    pfrom->AskFor(CInv(MSG_BLOCK, *i));
                }

                AdditionalBlocksToFetch.clear();

                LOCK2(cs_vNodes, pfrom->cs_alterSyncRanges);
                //Scope for lock
                pfrom->RangesToSync.clear();
                CBlockIndex* pFetchRanges = pindexBest;
                pfrom->NumRangesToSync=0;
                while(pFetchRanges->pprev)
                {
                    if(pFetchRanges->IsPlaceHolderBlock())
                    {
                        uint256 from = pFetchRanges->pprev->GetBlockHash();;
                        uint256 to = pFetchRanges->pnext->GetBlockHash();

                        pfrom->RangesToSync.push_front(std::make_pair(from, to));
                        pfrom->NumRangesToSync++;
                    }
                    else
                    {
                        numSyncedHeaders++;
                    }
                    pFetchRanges = pFetchRanges->pprev;
                }

                if (pfrom->NumRangesToSync == 0)
                {
                    TransitionLoadState(pfrom);
                }
                else
                {
                    //Distribute header fetching amongst as many other nodes as possible.
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    {
                        pnode->syncFrom = uint256(0);
                        pnode->syncTo = uint256(0);
                        pnode->isSyncing = false;
                        PushGetHeaderRange(pnode);
                    }
                }
            }
        }
        break;
        case LoadState_SyncAllHeaders:
        {
            assert(currentClientMode != ClientFull);

            switch(currentClientMode)
            {
                case ClientHybrid:
                {
                    currentLoadState = LoadState_SyncAllBlocks;

                    numSyncedBlocks = 0;
                    CBlockIndex* pIndex = pindexGenesisBlock;
                    while (pIndex && pIndex != pindexBest)
                    {
                        if (!pIndex->IsHeaderOnly())
                            numSyncedBlocks++;
                        pIndex = pIndex->pnext;
                    }

                    CreateBlockRanges(pfrom, mapBlockIndex[hashGenesisBlock], pindexBest);

                    if (pfrom->NumRangesToSync == 0)
                    {
                        TransitionLoadState(pfrom);
                    }
                }
                break;
                case ClientLight:
                {
                    //fixme: LIGHT do a scan here for any unloaded blocks that *are* ours.
                    currentLoadState = LoadState_AcceptingNewBlocks;
                }
                break;
                default:
                    assert(0);
            }
        }
        break;
        case LoadState_SyncAllBlocks:
        {
            currentLoadState = LoadState_VerifyAllBlocks;
        }
        case LoadState_VerifyAllBlocks:
        {
            if (currentClientMode == ClientLight)
            {
                currentLoadState = LoadState_AcceptingNewBlocks;
            }
            else
            {
                NewThread(VerifyAllBlocks, NULL);
                //jump start block synchronization
                PushGetBlocksFromHash(pfrom, pindexBest->GetBlockHash(), uint256(0));
            }
        }
        break;
        case LoadState_AcceptingNewBlocks:
        {
            // End state - nothing to do.
        }
    }
    return true;
}

void TransitionFromFullMode(ClientMode newMode)
{
    switch(newMode)
    {
        case ClientFull:
            // Nothing to do.
            return;
        case ClientHybrid:
        case ClientLight:
            if (!pindexGenesisBlock->IsVerified())
                UpgradeToHybridBlockchain(pindexGenesisBlock);
            currentLoadState = LoadState_Connect;
            ForceTransitionOnNextNodeActivity = true;
            return;
    }
}

void TransitionFromHybridToLightMode(ClientMode newMode)
{
    LOCK2(cs_ChangeLoadState, cs_vNodes);
    switch(currentLoadState)
    {
        case LoadState_SyncAllHeaders:
        case LoadState_SyncAllBlocks:
        {
            currentLoadState = LoadState_AcceptingNewBlocks;
            CNode::RangesToSync.clear();
            CNode::NumRangesToSync = 0;
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                pnode->syncFrom = uint256(0);
                pnode->syncTo = uint256(0);
                pnode->isSyncing = false;
            }
        }
        return;
        case LoadState_VerifyAllBlocks:
        {
            currentLoadState = LoadState_AcceptingNewBlocks;
            abortVerifyAllBlocks = true;
            LOCK(cs_VerifyAllBlocks);
            abortVerifyAllBlocks = false;
        }
        return;
        default:
            return;
    }
    return;
}

void ResetBlockchain()
{
    currentLoadState = LoadState_Begin;
    abortVerifyAllBlocks = true;
    denyIncomingBlocks = true;
    LOCK2(cs_vNodes, cs_VerifyAllBlocks);
    abortVerifyAllBlocks = false;

#ifdef WIN32
        MilliSleep(1000);
#else
        sleep(1);
#endif


    pindexGenesisBlock = NULL;
    pindexBest = NULL;
    nBestChainTrust=0;

    // Wipe block file.
    FlushBlockFile();
    DeleteBlockFile();

    // Wipe block index.
    mapBlockIndex.clear();
    mapOrphanBlocks.clear();
    setStakeSeen.clear();

    reinit_blockindex();
    CreateGenesisBlock(true);

    // Start accepting new blocks again.
    currentLoadState = LoadState_Connect;
    ForceTransitionOnNextNodeActivity = true;
    denyIncomingBlocks = false;
}

void ResetPeers()
{
    CNode::ClearBanned();

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->ClearMisbehaving();
    }

    //fixme: Need to wipe entire addrman here.
}

void ShowWarningAndResetBlockchain(string strMessage)
{
    printf("%s",strMessage.c_str());
    #ifndef HEADLESS
    string strError = strprintf(QObject::tr("Sync error encountered: \n%s\n\nThe most likely cause of this error is a problem with your local blockchain, so the blockchain will now reset itself and sync again.\nShould you encounter this error repeatedly please seek assistance.").toStdString().c_str(), strMessage.c_str());
    #else
    string strError = strprintf("Sync error encountered: \n%s\n\nThe most likely cause of this error is a problem with your local blockchain, so the blockchain will now reset itself and sync again.\nShould you encounter this error repeatedly please seek assistance.", strMessage.c_str());
    #endif
    uiInterface.ThreadSafeMessageBox(strError, "Pandacoin", CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION | CClientUIInterface::MODAL);
    ResetBlockchain();
    ResetPeers();
}

void TransitionFromHybridToFullMode(ClientMode newMode)
{
    ResetBlockchain();
    ResetPeers();
}

void TransitionFromHybridMode(ClientMode newMode)
{
    switch(newMode)
    {
        case ClientHybrid:
            // Nothing to do.
            return;
        case ClientLight:
            TransitionFromHybridToLightMode(newMode);
            return;
        case ClientFull:
            TransitionFromHybridToFullMode(newMode);
            return;
    }
}

void TransitionFromLightMode(ClientMode newMode)
{
    switch(newMode)
    {
        case ClientLight:
            // Nothing to do.
            return;
        case ClientHybrid:
            currentLoadState = LoadState_Connect;
            ForceTransitionOnNextNodeActivity = true;
            return;
        case ClientFull:
            TransitionFromHybridToFullMode(newMode);
            return;
    }
}

void TransitionClientMode(ClientMode oldMode, ClientMode newMode)
{
    switch(oldMode)
    {
        case ClientFull:
        {
            TransitionFromFullMode(newMode);
        }
        break;
        case ClientHybrid:
        {
           TransitionFromHybridMode(newMode);
        }
        break;
        case ClientLight:
        {
           TransitionFromLightMode(newMode);
        }
        break;
    }
}

// What load state the client is in i.e. Is it still busy synchronising the block chain.
extern LoadState currentLoadState;

bool fNewBlockChain=false;

int64_t numSyncedHeaders = 0;
int64_t numSyncedBlocks = 0;
int64_t numSyncedEpochBlocks = 0;
std::set<uint256> AdditionalBlocksToFetch;

int numBlocksToVerify;
int numBlocksVerified;

unsigned int firstWalletTxTime = std::numeric_limits<unsigned int>::max();

//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

// These functions dispatch to one or all registered wallets


void RegisterWallet(CWallet* pwalletIn)
{
    {
        LOCK(cs_setpwalletRegistered);
        setpwalletRegistered.insert(pwalletIn);
    }
}

void UnregisterWallet(CWallet* pwalletIn)
{
    {
        LOCK(cs_setpwalletRegistered);
        setpwalletRegistered.erase(pwalletIn);
    }
}

// check whether the passed transaction is from us
bool static IsFromMe(CTransaction& tx)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        if (pwallet->IsFromMe(tx))
            return true;
    return false;
}

// get the wallet transaction with the given hash (if it exists)
bool static GetTransaction(const uint256& hashTx, CWalletTx& wtx)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        if (pwallet->GetTransaction(hashTx,wtx))
            return true;
    return false;
}

// erases transaction with the given hash from all wallets
void static EraseFromWallets(uint256 hash)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->EraseFromWallet(hash);
}

// make sure all wallets know about the given transaction, in the given block
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock, bool fUpdate, bool fConnect)
{
    if (!fConnect)
    {
        // ppcoin: wallets need to refund inputs when disconnecting coinstake
        if (tx.IsCoinStake())
        {
            BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
                if (pwallet->IsFromMe(tx))
                    pwallet->DisableTransaction(tx);
        }
        return;
    }

    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->AddToWalletIfInvolvingMe(tx, pblock, fUpdate);
}

// notify wallets about a new best chain
void static SetBestChain(const CBlockLocator& loc)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->SetBestChain(loc);
}

// notify wallets about an updated transaction
void static UpdatedTransaction(const uint256& hashTx)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->UpdatedTransaction(hashTx);
}

// dump all wallets
void static PrintWallets(const CBlock& block)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->PrintWallet(block);
}

// notify wallets about an incoming inventory (for request counts)
void static Inventory(const uint256& hash)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->Inventory(hash);
}

// ask wallets to resend their transactions
void ResendWalletTransactions(bool fForce)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->ResendWalletTransactions(fForce);
}







//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:

    size_t nSize = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);

    if (nSize > 5000)
    {
        printf("ignoring large orphan tx (size: %" PRIszu ", hash: %s)\n", nSize, hash.ToString().substr(0,10).c_str());
        return false;
    }

    mapOrphanTransactions[hash] = tx;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    printf("stored orphan tx %s (mapsz %" PRIszu ")\n", hash.ToString().substr(0,10).c_str(),
        mapOrphanTransactions.size());
    return true;
}

void static EraseOrphanTx(uint256 hash)
{
    if (!mapOrphanTransactions.count(hash))
        return;
    const CTransaction& tx = mapOrphanTransactions[hash];
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        mapOrphanTransactionsByPrev[txin.prevout.hash].erase(hash);
        if (mapOrphanTransactionsByPrev[txin.prevout.hash].empty())
            mapOrphanTransactionsByPrev.erase(txin.prevout.hash);
    }
    mapOrphanTransactions.erase(hash);
}

unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, CTransaction>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}







//////////////////////////////////////////////////////////////////////////////
//
// CTransaction and CTxIndex
//

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout, CTxIndex& txindexRet)
{
    SetNull();
    if (!txdb.ReadTxIndex(prevout.hash, txindexRet))
        return false;
    if (!ReadFromDisk(txindexRet.pos))
        return false;
    if (prevout.n >= vout.size())
    {
        SetNull();
        return false;
    }
    return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout)
{
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::ReadFromDisk(COutPoint prevout)
{
    CTxDB txdb("r");
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool CTxOut::IsDust() const
{
    // Pandacoin: IsDust() detection disabled, allows any valid dust to be relayed.
    // The fees imposed on each dust txo is considered sufficient spam deterrant.
    return false;
}

bool CTransaction::IsStandard(string& strReason) const
{
    if (nVersion > CTransaction::CURRENT_VERSION || nVersion < 1)
        return false;

    if (!IsFinal()) {
        strReason = "not-final";
        return false;
    }

    unsigned int sz = this->GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz >= MAX_STANDARD_TX_SIZE) {
        strReason = "tx-size";
        return false;
    }

    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        // Biggest 'standard' txin is a 3-signature 3-of-3 CHECKMULTISIG
        // pay-to-script-hash, which is 3 ~80-byte signatures, 3
        // ~65-byte public keys, plus a few script ops.
        if (txin.scriptSig.size() > 500) {
            strReason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            strReason = "scriptsig-not-pushonly";
            return false;
        }
    }
    unsigned int nDataOut = 0;
    txnouttype whichType;
    BOOST_FOREACH(const CTxOut& txout, vout) {
    if (!::IsStandard(txout.scriptPubKey, whichType)) {
            strReason = "scriptpubkey";
            return false;
        }
        if (whichType == TX_NULL_DATA)
            nDataOut++;
        if (txout.IsDust()) {
            strReason = "dust";
            return false;
        }
    }
      // only one OP_RETURN txout is permitted
    if (nDataOut > 1) {
        return false;
    }
    return true;
}

//
// Check transaction inputs, and make sure any
// pay-to-script-hash transactions are evaluating IsStandard scripts
//
// Why bother? To avoid denial-of-service attacks; an attacker
// can submit a standard HASH... OP_EQUAL transaction,
// which will get accepted into blocks. The redemption
// script can be anything; an attacker could use a very
// expensive-to-check-upon-redemption script like:
//   DUP CHECKSIG DROP ... repeated 100 times... OP_1
//
bool CTransaction::AreInputsStandard(const MapPrevTx& mapInputs) const
{
    if (IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const CTxOut& prev = GetOutputFor(vin[i], mapInputs);

        vector<vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig the
        // IsStandard() call returns false
        vector<vector<unsigned char> > stack;
        if (!EvalScript(stack, vin[i].scriptSig, *this, i, 0))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            vector<vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (!Solver(subscript, whichType2, vSolutions2))
                return false;
            if (whichType2 == TX_SCRIPTHASH)
                return false;

            int tmpExpected;
            tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
            if (tmpExpected < 0)
                return false;
            nArgsExpected += tmpExpected;
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

unsigned int
CTransaction::GetLegacySigOpCount() const
{
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    BOOST_FOREACH(const CTxOut& txout, vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}


int CMerkleTx::SetMerkleBranch(const CBlock* pblock)
{
    if (fClient)
    {
        if (hashBlock == 0)
            return 0;
    }
    else
    {
        CBlock blockTmp;
        if (pblock == NULL)
        {
            // Load the block this tx is in
            CTxIndex txindex;
            if (!CTxDB("r").ReadTxIndex(GetHash(), txindex))
                return 0;
            if (!blockTmp.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos))
                return 0;
            pblock = &blockTmp;
        }

        // Update the tx's hashBlock
        hashBlock = pblock->GetHash();

        // Locate the transaction
        for (nIndex = 0; nIndex < (int)pblock->vtx.size(); nIndex++)
            if (pblock->vtx[nIndex] == *(CTransaction*)this)
                break;
        if (nIndex == (int)pblock->vtx.size())
        {
            vMerkleBranch.clear();
            nIndex = -1;
            printf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
            return 0;
        }

        // Fill in merkle branch
        vMerkleBranch = pblock->GetMerkleBranch(nIndex);
    }

    // Is the tx in a block that's in the main chain
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    return pindexBest->nHeight - pindex->nHeight + 1;
}







bool CTransaction::CheckTransaction() const
{
    // Basic checks that don't depend on any context
    if (vin.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vin empty"));
    if (vout.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vout empty"));
    // Size limits
    if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CTransaction::CheckTransaction() : size limits failed"));

    // Check for negative or overflow output values
    int64_t nValueOut = 0;
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        const CTxOut& txout = vout[i];

        if (txout.IsEmpty() && !IsCoinBase() && !IsCoinStake())
            return DoS(100, error("CTransaction::CheckTransaction() : txout empty for user transaction"));
        if (txout.nValue < 0)
            return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue negative"));
        if (txout.nValue > MAX_MONEY)
            return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue too high"));
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return DoS(100, error("CTransaction::CheckTransaction() : txout total out of range"));
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return false;
        vInOutPoints.insert(txin.prevout);
    }

    if (IsCoinBase())
    {
        if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
            return DoS(100, error("CTransaction::CheckTransaction() : coinbase script size is invalid"));
    }
    else
    {
        BOOST_FOREACH(const CTxIn& txin, vin)
            if (txin.prevout.IsNull())
                return DoS(10, error("CTransaction::CheckTransaction() : prevout is null"));
    }

    return true;
}

int64_t CTransaction::GetMinFee(unsigned int nBlockSize, enum GetMinFee_mode mode, unsigned int nBytes, bool fAllowFree) const
{
    // Base fee is either MIN_TX_FEE or MIN_RELAY_TX_FEE
    int64_t nBaseFee = (mode == GMF_RELAY) ? MIN_RELAY_TX_FEE : MIN_TX_FEE;

    unsigned int nNewBlockSize = nBlockSize + nBytes;
    int64_t nMinFee = (1 + (int64_t)nBytes / 1000) * nBaseFee;

    if (fAllowFree)
    {
        if (nBlockSize == 1)
        {
            // Transactions under 10K are free
            // (about 4500bc if made of 50bc inputs)
            if (nBytes < 10000)
                nMinFee = 0;
        }
        else
        {
            // Free transaction area
            if (nNewBlockSize < 27000)
                nMinFee = 0;
        }
    }

    // Pandacoin
    // To limit dust spam, add nBaseFee for each output less than DUST_SOFT_LIMIT
    BOOST_FOREACH(const CTxOut& txout, vout)
        if (txout.nValue < DUST_SOFT_LIMIT)
            nMinFee += nBaseFee;

    // Raise the price as the block approaches full
    if (nBlockSize != 1 && nNewBlockSize >= MAX_BLOCK_SIZE_GEN/2)
    {
        if (nNewBlockSize >= MAX_BLOCK_SIZE_GEN)
            return MAX_MONEY;
        nMinFee *= MAX_BLOCK_SIZE_GEN / (MAX_BLOCK_SIZE_GEN - nNewBlockSize);
    }

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}


bool CTxMemPool::accept(CTxDB& txdb, CTransaction &tx, bool fCheckInputs,
                        bool* pfMissingInputs)
{
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!tx.CheckTransaction())
        return error("CTxMemPool::accept() : CheckTransaction failed");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return tx.DoS(100, error("CTxMemPool::accept() : coinbase as individual tx"));

    // ppcoin: coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return tx.DoS(100, error("CTxMemPool::accept() : coinstake as individual tx"));

    // To help v0.1.5 clients who would see it as a negative number
    if ((int64_t)tx.nLockTime > std::numeric_limits<int>::max())
        return error("CTxMemPool::accept() : not accepting nLockTime beyond 2038 yet");

    // Rather not work on nonstandard transactions (unless -testnet)
    string strNonStd;
    if (!fTestNet && !tx.IsStandard(strNonStd))
        return error("CTxMemPool::accept() : nonstandard transaction type");

    // Do we already have it?
    uint256 hash = tx.GetHash();
    {
        LOCK(cs);
        if (mapTx.count(hash))
            return false;
    }
    if (fCheckInputs)
        if (txdb.ContainsTx(hash))
            return false;

    // Check for conflicts with in-memory transactions
    CTransaction* ptxOld = NULL;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        COutPoint outpoint = tx.vin[i].prevout;
        if (mapNextTx.count(outpoint))
        {
            // Disable replacement feature for now
            return false;

            // Allow replacing with a newer version of the same transaction
            if (i != 0)
                return false;
            ptxOld = mapNextTx[outpoint].ptx;
            if (ptxOld->IsFinal())
                return false;
            if (!tx.IsNewerThan(*ptxOld))
                return false;
            for (unsigned int i = 0; i < tx.vin.size(); i++)
            {
                COutPoint outpoint = tx.vin[i].prevout;
                if (!mapNextTx.count(outpoint) || mapNextTx[outpoint].ptx != ptxOld)
                    return false;
            }
            break;
        }
    }

    if (fCheckInputs)
    {
        MapPrevTx mapInputs;
        map<uint256, CTxIndex> mapUnused;
        bool fInvalid = false;
        if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            if (fInvalid)
                return error("CTxMemPool::accept() : FetchInputs found invalid tx %s", hash.ToString().substr(0,10).c_str());
            if (pfMissingInputs)
                *pfMissingInputs = true;
            return false;
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (!tx.AreInputsStandard(mapInputs) && !fTestNet)
            return error("CTxMemPool::accept() : nonstandard transaction input");

        // Note: if you modify this code to accept non-standard transactions, then
        // you should add code here to check that the transaction does a
        // reasonable number of ECDSA signature verifications.

        int64_t nFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();
        unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

        // Don't accept it if it can't get into a block
        int64_t txMinFee = tx.GetMinFee(1000, GMF_RELAY, nSize, true);
        if (nFees < txMinFee)
            return error("CTxMemPool::accept() : not enough fees %s, %" PRId64 " < %" PRId64,
                         hash.ToString().c_str(),
                         nFees, txMinFee);

        // Continuously rate-limit free transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (nFees < MIN_RELAY_TX_FEE)
        {
            static CCriticalSection cs;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            {
                LOCK(cs);
                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000 && !IsFromMe(tx))
                    return error("CTxMemPool::accept() : free transaction rejected by rate limiter");
                if (fDebug)
                    printf("Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
                dFreeCount += nSize;
            }
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1), pindexBest, false, false))
        {
            return error("CTxMemPool::accept() : ConnectInputs failed %s", hash.ToString().substr(0,10).c_str());
        }
    }

    // Store transaction in memory
    {
        LOCK(cs);
        if (ptxOld)
        {
            printf("CTxMemPool::accept() : replacing tx %s with new version\n", ptxOld->GetHash().ToString().c_str());
            remove(*ptxOld);
        }
        addUnchecked(hash, tx);
    }

    ///// are we sure this is ok when loading transactions or restoring block txes
    // If updated, erase old tx from wallet
    if (ptxOld)
        EraseFromWallets(ptxOld->GetHash());

    printf("CTxMemPool::accept() : accepted %s (poolsz %" PRIszu ")\n",
           hash.ToString().substr(0,10).c_str(),
           mapTx.size());
    return true;
}

bool CTransaction::AcceptToMemoryPool(CTxDB& txdb, bool fCheckInputs, bool* pfMissingInputs)
{
    return mempool.accept(txdb, *this, fCheckInputs, pfMissingInputs);
}

bool CTxMemPool::addUnchecked(const uint256& hash, CTransaction &tx)
{
    // Add to memory pool without checking anything.  Don't call this directly,
    // call CTxMemPool::accept to properly check the transaction first.
    {
        mapTx[hash] = tx;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
            mapNextTx[tx.vin[i].prevout] = CInPoint(&mapTx[hash], i);
        nTransactionsUpdated++;
    }
    return true;
}


bool CTxMemPool::remove(const CTransaction &tx, bool fRecursive)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        uint256 hash = tx.GetHash();
        if (mapTx.count(hash))
        {
            if (fRecursive) {
                for (unsigned int i = 0; i < tx.vout.size(); i++) {
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                    if (it != mapNextTx.end())
                        remove(*it->second.ptx, true);
                }
            }
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
                mapNextTx.erase(txin.prevout);
            mapTx.erase(hash);
            nTransactionsUpdated++;
        }
    }
    return true;
}

bool CTxMemPool::removeConflicts(const CTransaction &tx)
{
    // Remove transactions which depend on inputs of tx, recursively
    LOCK(cs);
    BOOST_FOREACH(const CTxIn &txin, tx.vin) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx)
                remove(txConflict, true);
        }
    }
    return true;
}

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
    mapNextTx.clear();
    ++nTransactionsUpdated;
}

void CTxMemPool::queryHashes(std::vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size());
    for (map<uint256, CTransaction>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back((*mi).first);
}




int CMerkleTx::GetDepthInMainChainINTERNAL(CBlockIndex* &pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;

    // Find the block it claims to be in
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return pindexBest->nHeight - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(CBlockIndex* &pindexRet) const
{
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    int nMaturity = IsCoinBase() ? nCoinbaseMaturity : nCoinstakeMaturity;
    return max(0, (nMaturity+20) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(CTxDB& txdb, bool fCheckInputs)
{
    if (fClient)
    {
        if (!IsInMainChain() && !ClientConnectInputs())
            return false;
        return CTransaction::AcceptToMemoryPool(txdb, fCheckInputs);
    }
    else
    {
        return CTransaction::AcceptToMemoryPool(txdb, fCheckInputs);
    }
}

bool CMerkleTx::AcceptToMemoryPool()
{
    CTxDB txdb("r");
    return AcceptToMemoryPool(txdb);
}



bool CWalletTx::AcceptWalletTransaction(CTxDB& txdb, bool fCheckInputs)
{

    {
        LOCK(mempool.cs);
        // Add previous supporting transactions first
        BOOST_FOREACH(CMerkleTx& tx, vtxPrev)
        {
            if (!(tx.IsCoinBase() || tx.IsCoinStake()))
            {
                uint256 hash = tx.GetHash();
                if (!mempool.exists(hash) && !txdb.ContainsTx(hash))
                    tx.AcceptToMemoryPool(txdb, fCheckInputs);
            }
        }
        return AcceptToMemoryPool(txdb, fCheckInputs);
    }
    return false;
}

bool CWalletTx::AcceptWalletTransaction()
{
    CTxDB txdb("r");
    return AcceptWalletTransaction(txdb);
}

int CTxIndex::GetDepthInMainChain() const
{
    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(pos.nFile, pos.nBlockPos, false))
        return 0;
    // Find the block in the index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;
    return 1 + nBestHeight - pindex->nHeight;
}

// Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock)
{
    {
        LOCK(cs_main);
        {
            LOCK(mempool.cs);
            if (mempool.exists(hash))
            {
                tx = mempool.lookup(hash);
                return true;
            }
        }
        CTxDB txdb("r");
        CTxIndex txindex;
        if (tx.ReadFromDisk(txdb, COutPoint(hash, 0), txindex))
        {
            CBlock block;
            if (block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                hashBlock = block.GetHash();
            return true;
        }
    }
    return false;
}








//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex *pblockindex;
    if (nHeight < nBestHeight / 2)
        pblockindex = pindexGenesisBlock;
    else
        pblockindex = pindexBest;
    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
        pblockindex = pblockindexFBBHLast;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    while (pblockindex->nHeight < nHeight)
        pblockindex = pblockindex->pnext;
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
}

bool CBlock::ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions)
{
    if (!fReadTransactions)
    {
        *this = pindex->GetBlockHeader();
        return true;
    }
    if (!ReadFromDisk(pindex->nFile, pindex->nBlockPos, fReadTransactions))
        return false;
    if (GetHash() != pindex->GetBlockHash())
    {
        return error("CBlock::ReadFromDisk() : GetHash() doesn't match index");
    }

    return true;
}

uint256 static GetOrphanRoot(const CBlock* pblock)
{
    // Work back to the first block in the orphan chain
    while (mapOrphanBlocks.count(pblock->hashPrevBlock))
        pblock = mapOrphanBlocks[pblock->hashPrevBlock];
    return pblock->GetHash();
}

// ppcoin: find block wanted by given orphan block
uint256 WantedByOrphan(const CBlock* pblockOrphan)
{
    // Work back to the first block in the orphan chain
    while (mapOrphanBlocks.count(pblockOrphan->hashPrevBlock))
        pblockOrphan = mapOrphanBlocks[pblockOrphan->hashPrevBlock];
    return pblockOrphan->hashPrevBlock;
}

int static generateMTRandom(unsigned int s, int range)
{
    boost::mt19937 gen(s);
    boost::uniform_int<> dist(1, range);
    return dist(gen);
}

// miner's coin base reward
int64_t GetProofOfWorkReward(int nHeight, int64_t nFees, uint256 prevHash)
{
    int64_t nSubsidy = 10000 * COIN;

    std::string cseed_str = prevHash.ToString().substr(7,7);
    const char* cseed = cseed_str.c_str();
    long seed = hex2long(cseed);
    int rand = generateMTRandom(seed, 999999);
    int rand1 = 0;

    if(nHeight < 50000)
    {
        nSubsidy = (1 + rand) * COIN;
    }
    else if(nHeight < 63500)
    {
        cseed_str = prevHash.ToString().substr(7,7);
        cseed = cseed_str.c_str();
        seed = hex2long(cseed);
        rand1 = generateMTRandom(seed, 499999);
        nSubsidy = (1 + rand1) * COIN;
    }
    else
    {
        // Reduce until either PoS is done or plans for improved rewards are in place
        nSubsidy = 50000 * COIN;
    }

    if (fDebug && GetBoolArg("-printcreation"))
        printf("GetProofOfWorkReward() : create=%s nSubsidy=%" PRId64 "\n", FormatMoney(nSubsidy).c_str(), nSubsidy);

    return nSubsidy + nFees;
}

// miner's coin stake reward based on coin age spent (coin-days)
int64_t GetProofOfStakeReward(int64_t nCoinAge, int64_t nFees)
{
    int64_t nSubsidy = nCoinAge * COIN_YEAR_REWARD * 33 / (365 * 33 + 8);

    if (fDebug && GetBoolArg("-printcreation"))
        printf("GetProofOfStakeReward(): create=%s nCoinAge=%" PRId64 "\n", FormatMoney(nSubsidy).c_str(), nCoinAge);

    return nSubsidy + nFees;
}

//
// maximum nBits value could possible be required nTime after
//
unsigned int ComputeMaxBits(CBigNum bnTargetLimit, unsigned int nBase, int64_t nTime)
{
    CBigNum bnResult;
    bnResult.SetCompact(nBase);
    bnResult *= 2;
    while (nTime > 0 && bnResult < bnTargetLimit)
    {
        // Maximum 200% adjustment per day...
        bnResult *= 2;
        nTime -= 24 * 60 * 60;
    }
    if (bnResult > bnTargetLimit)
        bnResult = bnTargetLimit;
    return bnResult.GetCompact();
}

//
// minimum amount of work that could possibly be required nTime after
// minimum proof-of-work required was nBase
//
unsigned int ComputeMinWork(unsigned int nBase, int64_t nTime)
{
    // Testnet has min-difficulty blocks
    // after GetTargetSpacing()*2 time between blocks:
    if (fTestNet && nTime > GetTargetSpacing()*2)
        return GetWorkLimit().GetCompact();

    return ComputeMaxBits(GetWorkLimit(), nBase, nTime);
}

//
// minimum amount of stake that could possibly be required nTime after
// minimum proof-of-stake required was nBase
//
unsigned int ComputeMinStake(unsigned int nBase, int64_t nTime, unsigned int nBlockTime)
{
    return ComputeMaxBits(GetWorkLimit(), nBase, nTime);
}


// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

unsigned int static GetNextWorkRequired_V1(const CBlockIndex* pindexLast, const CBlock *pblock)
{
    unsigned int nProofOfWorkLimit = (GetWorkLimit()).GetCompact();

    if (pindexLast == NULL)
        return nProofOfWorkLimit; // genesis block

    // Only change once per interval
    if ((pindexLast->nHeight+1) % GetInterval() != 0)
    {
        // Special difficulty rule for testnet:
        if (fTestNet)
        {
            // If the new block's timestamp is more than 2* GetTargetSpacing() minutes
            // then allow mining of a min-difficulty block.
            if (pblock->nTime > pindexLast->nTime + GetTargetSpacing()*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % GetInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Pandacoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = GetInterval() - 1;
    if ((pindexLast->nHeight+1) != GetInterval())
        blockstogoback = GetInterval();

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    printf("  nActualTimespan = %ld before bounds\n", nActualTimespan);
    if(pindexLast->nHeight+1 > 10000)
    {
        if (nActualTimespan < GetTargetTimespan()/4)
            nActualTimespan = GetTargetTimespan()/4;
        if (nActualTimespan > GetTargetTimespan()*4)
            nActualTimespan = GetTargetTimespan()*4;
    }
    else if(pindexLast->nHeight+1 > 5000)
    {
        if (nActualTimespan < GetTargetTimespan()/8)
           nActualTimespan = GetTargetTimespan()/8;
        if (nActualTimespan > GetTargetTimespan()*4)
            nActualTimespan = GetTargetTimespan()*4;
    }
    else
    {
        if (nActualTimespan < GetTargetTimespan()/16)
            nActualTimespan = GetTargetTimespan()/16;
        if (nActualTimespan > GetTargetTimespan()*4)
            nActualTimespan = GetTargetTimespan()*4;
   }

    // Retarget
    CBigNum bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= GetTargetTimespan();

    if (bnNew > GetWorkLimit())
        bnNew = GetWorkLimit();

    /// debug print
    if (fDebug || !IsInitialBlockDownload())
    {
        printf("GetNextWorkRequired RETARGET\n");
        printf("nTargetTimespan = %ld    nActualTimespan = %ld\n", GetTargetTimespan(), nActualTimespan);
        printf("Before: %08x  %s\n", pindexLast->nBits, CBigNum().SetCompact(pindexLast->nBits).getuint256().ToString().c_str());
        printf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.getuint256().ToString().c_str());
    }

    return bnNew.GetCompact();
}

unsigned int static KimotoGravityWell(const CBlockIndex* pindexLast, uint64_t TargetBlocksSpacingSeconds, uint64_t PastBlocksMin, uint64_t PastBlocksMax)
{
    /* current difficulty formula, megacoin - kimoto gravity well */
    const CBlockIndex  *BlockLastSolved = pindexLast;
    const CBlockIndex  *BlockReading    = pindexLast;

    uint64_t	PastBlocksMass		= 0;
    int64_t		PastRateActualSeconds	= 0;
    int64_t		PastRateTargetSeconds 	= 0;
    double		PastRateAdjustmentRatio = double(1);
    CBigNum		PastDifficultyAverage;
    CBigNum		PastDifficultyAveragePrev;
    double		EventHorizonDeviation;
    double		EventHorizonDeviationFast;
    double		EventHorizonDeviationSlow;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return GetWorkLimit().GetCompact(); }

    int64_t LatestBlockTime = BlockLastSolved->GetBlockTime();
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
            if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
            PastBlocksMass++;

            if (i == 1)     { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
            else            { PastDifficultyAverage = ((CBigNum().SetCompact(BlockReading->nBits) - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev; }
            PastDifficultyAveragePrev = PastDifficultyAverage;

            if (LatestBlockTime < BlockReading->GetBlockTime()) {
                    if (BlockReading->nHeight > 71000)
                            LatestBlockTime = BlockReading->GetBlockTime();
            }
            PastRateActualSeconds = LatestBlockTime - BlockReading->GetBlockTime();
            PastRateTargetSeconds                   = TargetBlocksSpacingSeconds * PastBlocksMass;
            PastRateAdjustmentRatio                 = double(1);
            if (BlockReading->nHeight > 71000) {
                    if (PastRateActualSeconds < 1) { PastRateActualSeconds = 1; }
            } else {
                    if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
            }
            if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            PastRateAdjustmentRatio                 = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
            }
            EventHorizonDeviation                   = 1 + (0.7084 * pow((double(PastBlocksMass)/double(28.2)), -1.228));
            EventHorizonDeviationFast               = EventHorizonDeviation;
            EventHorizonDeviationSlow               = 1 / EventHorizonDeviation;

            if (PastBlocksMass >= PastBlocksMin) {
                    if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast)) { assert(BlockReading); break; }
            }
            if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
            BlockReading = BlockReading->pprev;
    }
    CBigNum bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            bnNew *= PastRateActualSeconds;
            bnNew /= PastRateTargetSeconds;
    }
    if (bnNew > GetWorkLimit())
    {
            bnNew = GetWorkLimit();
    }

    /// debug print
    if (fDebug || !IsInitialBlockDownload())
    {
        printf("Difficulty Retarget - Kimoto Gravity Well\n");
        printf("PastRateAdjustmentRatio = %g\n", PastRateAdjustmentRatio);
        printf("Before: %08x  %s\n", BlockLastSolved->nBits, CBigNum().SetCompact(BlockLastSolved->nBits).getuint256().ToString().c_str());
        printf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.getuint256().ToString().c_str());
    }

    return bnNew.GetCompact();
}

unsigned int static GetNextWorkRequired_V2(const CBlockIndex* pindexLast)
{
    int64_t	BlocksTargetSpacing			= 60; // 60 seconds

    // Accelerate testnet past normal net
    if (fTestNet && pindexLast->nHeight < 550000)
    {
        if (pindexLast->nHeight > 136167)
        {
            BlocksTargetSpacing = 30;
        }
        else
        {
            BlocksTargetSpacing = 1;
        }
    }

    unsigned int		TimeDaySeconds				= 60 * 60 * 24;
    int64_t				PastSecondsMin				= TimeDaySeconds * 0.01;
    int64_t				PastSecondsMax				= TimeDaySeconds * 0.14;
    uint64_t				PastBlocksMin				= PastSecondsMin / BlocksTargetSpacing;
    uint64_t				PastBlocksMax				= PastSecondsMax / BlocksTargetSpacing;

    return KimotoGravityWell(pindexLast, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, bool fProofOfStake, const CBlock *pblock)
{
    CBigNum bnTargetLimit = GetWorkLimit();

    if(fProofOfStake)
    {
        const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
        if (pindexPrev->pprev == NULL)
            return bnTargetLimit.GetCompact(); // first block
        const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
        if (pindexPrevPrev->pprev == NULL)
            return bnTargetLimit.GetCompact(); // second block

        int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();
        if (nActualSpacing < 0)
            nActualSpacing = GetTargetSpacing();

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        CBigNum bnNew;
        bnNew.SetCompact(pindexPrev->nBits);
        bnNew *= ((GetInterval() - 1) * GetTargetSpacing() + nActualSpacing + nActualSpacing);
        bnNew /= ((GetInterval() + 1) * GetTargetSpacing());

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    }

    int DiffMode = 1;
    if (fTestNet) {
            if (pindexLast->nHeight+1 >= 50) { DiffMode = 2; }
    }
    else {
            if (pindexLast->nHeight+1 >= 6500) { DiffMode = 2; }
    }

    if (DiffMode == 1)
        return GetNextWorkRequired_V1(pindexLast, pblock);

    return GetNextWorkRequired_V2(pindexLast);
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    // Check range
    if (bnTarget <= 0 || bnTarget > GetWorkLimit(0))
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget.getuint256())
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

// Return maximum amount of blocks that other nodes claim to have
int GetNumBlocksOfPeers()
{
    //fixme: Forcing a median result even when only 1 peer.
    if(cPeerBlockCounts.size()==2)
    {
        return std::max(cPeerBlockCounts.sorted()[1], Checkpoints::GetTotalBlocksEstimate());
    }
    else
    {
        return std::max(cPeerBlockCounts.median(), Checkpoints::GetTotalBlocksEstimate());
    }
}

bool IsInitialBlockDownload()
{
    if(currentClientMode != ClientFull && currentLoadState != LoadState_VerifyAllBlocks && currentLoadState != LoadState_AcceptingNewBlocks)
        return true;
    if (pindexBest == NULL || nBestHeight < Checkpoints::GetTotalBlocksEstimate())
        return true;
    static int64_t nLastUpdate;
    static CBlockIndex* pindexLastBest;
    if (pindexBest != pindexLastBest)
    {
        pindexLastBest = pindexBest;
        nLastUpdate = GetTime();
    }
    return (GetTime() - nLastUpdate < 15 &&
            pindexBest->GetBlockTime() < GetTime() - 8 * 60 * 60);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (pindexNew->nChainTrust > nBestInvalidTrust)
    {
        nBestInvalidTrust = pindexNew->nChainTrust;
        CTxDB().WriteBestInvalidTrust(CBigNum(nBestInvalidTrust));
        uiInterface.NotifyBlocksChanged();
    }

    uint256 nBestInvalidBlockTrust = pindexNew->nChainTrust - pindexNew->pprev->nChainTrust;
    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    printf("InvalidChainFound: invalid block=%s  height=%d  trust=%s  blocktrust=%" PRId64 "  date=%s\n",
      pindexNew->GetBlockHash().ToString().substr(0,20).c_str(), pindexNew->nHeight,
      CBigNum(pindexNew->nChainTrust).ToString().c_str(), nBestInvalidBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexNew->GetBlockTime()).c_str());
    printf("InvalidChainFound:  current best=%s  height=%d  trust=%s  blocktrust=%" PRId64 "  date=%s\n",
      hashBestChain.ToString().substr(0,20).c_str(), nBestHeight,
      CBigNum(pindexBest->nChainTrust).ToString().c_str(),
      nBestBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());
}


void CBlock::UpdateTime(const CBlockIndex* pindexPrev)
{
    nTime = max(GetBlockTime(), GetAdjustedTime());
}





bool CTransaction::DisconnectInputs(CTxDB& txdb)
{
    // Relinquish previous transactions' spent pointers
    if (!IsCoinBase())
    {
        BOOST_FOREACH(const CTxIn& txin, vin)
        {
            COutPoint prevout = txin.prevout;

            // Get prev txindex from disk
            CTxIndex txindex;
            if (!txdb.ReadTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : ReadTxIndex failed");

            if (prevout.n >= txindex.vSpent.size())
                return error("DisconnectInputs() : prevout.n out of range");

            // Mark outpoint as not spent
            txindex.vSpent[prevout.n].SetNull();

            // Write back
            if (!txdb.UpdateTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : UpdateTxIndex failed");
        }
    }

    // Remove transaction from index
    // This can fail if a duplicate of this transaction was in a chain that got
    // reorganized away. This is only possible if this transaction was completely
    // spent, so erasing it would be a no-op anyway.
    txdb.EraseTxIndex(*this);

    return true;
}


bool CTransaction::FetchInputs(CTxDB& txdb, const map<uint256, CTxIndex>& mapTestPool,
                               bool fBlock, bool fMiner, MapPrevTx& inputsRet, bool& fInvalid)
{
    // FetchInputs can return false either because we just haven't seen some inputs
    // (in which case the transaction should be stored as an orphan)
    // or because the transaction is malformed (in which case the transaction should
    // be dropped).  If tx is definitely invalid, fInvalid will be set to true.
    fInvalid = false;

    if (IsCoinBase())
        return true; // Coinbase transactions have no inputs to fetch.

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        COutPoint prevout = vin[i].prevout;
        if (inputsRet.count(prevout.hash))
            continue; // Got it already

        // Read txindex
        CTxIndex& txindex = inputsRet[prevout.hash].first;
        bool fFound = true;
        if ((fBlock || fMiner) && mapTestPool.count(prevout.hash))
        {
            // Get txindex from current proposed changes
            txindex = mapTestPool.find(prevout.hash)->second;
        }
        else
        {
            // Read txindex from txdb
            fFound = txdb.ReadTxIndex(prevout.hash, txindex);
        }
        if (!fFound && (fBlock || fMiner))
            return fMiner ? false : error("FetchInputs() : %s prev tx %s index entry not found", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());

        // Read txPrev
        CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (!fFound || txindex.pos == CDiskTxPos(1,1,1))
        {
            // Get prev tx from single transactions in memory
            {
                LOCK(mempool.cs);
                if (!mempool.exists(prevout.hash))
                    return error("FetchInputs() : %s mempool Tx prev not found %s", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());
                txPrev = mempool.lookup(prevout.hash);
            }
            if (!fFound)
                txindex.vSpent.resize(txPrev.vout.size());
        }
        else
        {
            // Get prev tx from disk
            if (!txPrev.ReadFromDisk(txindex.pos))
                return error("FetchInputs() : %s ReadFromDisk prev tx %s failed", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());
        }
    }

    // Make sure all prevout.n indexes are valid:
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const COutPoint prevout = vin[i].prevout;
        assert(inputsRet.count(prevout.hash) != 0);
        const CTxIndex& txindex = inputsRet[prevout.hash].first;
        const CTransaction& txPrev = inputsRet[prevout.hash].second;

        if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
        {
            // Revisit this if/when transaction replacement is implemented and allows
            // adding inputs:
            fInvalid = true;
            return DoS(100, error("FetchInputs() : %s prevout.n out of range %d %" PRIszu " %" PRIszu " prev tx %s\n%s", GetHash().ToString().substr(0,10).c_str(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString().substr(0,10).c_str(), txPrev.ToString().c_str()));
        }
    }

    return true;
}

const CTxOut& CTransaction::GetOutputFor(const CTxIn& input, const MapPrevTx& inputs) const
{
    MapPrevTx::const_iterator mi = inputs.find(input.prevout.hash);
    if (mi == inputs.end())
        throw std::runtime_error("CTransaction::GetOutputFor() : prevout.hash not found");

    const CTransaction& txPrev = (mi->second).second;
    if (input.prevout.n >= txPrev.vout.size())
        throw std::runtime_error("CTransaction::GetOutputFor() : prevout.n out of range");

    return txPrev.vout[input.prevout.n];
}

int64_t CTransaction::GetValueIn(const MapPrevTx& inputs) const
{
    if (IsCoinBase())
        return 0;

    int64_t nResult = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        //fixme:LIGHTHYBRID
        try
        {
            nResult += GetOutputFor(vin[i], inputs).nValue;
        }
        catch(...)
        {

        }
    }
    return nResult;

}

unsigned int CTransaction::GetP2SHSigOpCount(const MapPrevTx& inputs) const
{
    if (IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const CTxOut& prevout = GetOutputFor(vin[i], inputs);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(vin[i].scriptSig);
    }
    return nSigOps;
}

bool CTransaction::ConnectInputs(CTxDB& txdb, MapPrevTx inputs, map<uint256, CTxIndex>& mapTestPool, const CDiskTxPos& posThisTx,
    const CBlockIndex* pindexBlock, bool fBlock, bool fMiner, bool fForceFullConnect)
{
    if(currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
        fForceFullConnect = true;

    // Take over previous transactions' spent pointers
    // fBlock is true when this is called from AcceptBlock when a new best-block is added to the blockchain
    // fMiner is true when called from the internal bitcoin miner
    // ... both are false when called from CTransaction::AcceptToMemoryPool
    if (!IsCoinBase())
    {
        int64_t nValueIn = 0;
        int64_t nFees = 0;
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            COutPoint prevout = vin[i].prevout;
            //fixme:LIGHTHYBRID
            if(fForceFullConnect || inputs.count(prevout.hash) > 0)
            {
                assert(inputs.count(prevout.hash) > 0);
                CTxIndex& txindex = inputs[prevout.hash].first;
                CTransaction& txPrev = inputs[prevout.hash].second;

                if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
                    return DoS(100, error("ConnectInputs() : %s prevout.n out of range %d %" PRIszu " %" PRIszu " prev tx %s\n%s", GetHash().ToString().substr(0,10).c_str(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString().substr(0,10).c_str(), txPrev.ToString().c_str()));

                // If prev is coinbase or coinstake, check that it's matured
                if (txPrev.IsCoinBase() || txPrev.IsCoinStake())
                    for (const CBlockIndex* pindex = pindexBlock;
                       pindex && pindexBlock->nHeight - pindex->nHeight < (txPrev.IsCoinBase() ? nCoinbaseMaturity : nCoinstakeMaturity);
                       pindex = pindex->pprev)
                        if (pindex->nBlockPos == txindex.pos.nBlockPos && pindex->nFile == txindex.pos.nFile)
                            return error("ConnectInputs() : tried to spend %s at depth %d", txPrev.IsCoinBase() ? "coinbase" : "coinstake", pindexBlock->nHeight - pindex->nHeight);

                // Check for negative or overflow input values
                nValueIn += txPrev.vout[prevout.n].nValue;
                if (!MoneyRange(txPrev.vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                    return DoS(100, error("ConnectInputs() : txin values out of range"));
            }
        }
        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            COutPoint prevout = vin[i].prevout;
            //fixme:LIGHTHYBRID
            if(fForceFullConnect || inputs.count(prevout.hash) > 0)
            {
                assert(inputs.count(prevout.hash) > 0);
                CTxIndex& txindex = inputs[prevout.hash].first;
                CTransaction& txPrev = inputs[prevout.hash].second;

                // Check for conflicts (double-spend)
                // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
                // for an attacker to attempt to split the network.
                if (!txindex.vSpent[prevout.n].IsNull())
                    return fMiner ? false : error("ConnectInputs() : %s prev tx already used at %s", GetHash().ToString().substr(0,10).c_str(), txindex.vSpent[prevout.n].ToString().c_str());

                // Skip ECDSA signature verification when connecting blocks (fBlock=true)
                // before the last blockchain checkpoint. This is safe because block merkle hashes are
                // still computed and checked, and any change will be caught at the next checkpoint.
                if (!(fBlock && ((pindexBlock?pindexBlock->nHeight:nBestHeight) < Checkpoints::GetTotalBlocksEstimate())))
                {
                    // Verify signature
                    if (!VerifySignature(txPrev, *this, i, 0))
                    {
                        return DoS(100,error("ConnectInputs() : %s VerifySignature failed", GetHash().ToString().substr(0,10).c_str()));
                    }
                }

                // Mark outpoints as spent
                txindex.vSpent[prevout.n] = posThisTx;

                // Write back
                if (fBlock || fMiner)
                {
                    mapTestPool[prevout.hash] = txindex;
                }
            }
        }

        if (!IsCoinStake())
        {
            //fixme:LIGHTHYBRID
            if(fForceFullConnect)
            {
                if (nValueIn < GetValueOut())
                    return DoS(100, error("ConnectInputs() : %s value in %ld < value out %ld", GetHash().ToString().substr(0,10).c_str(), nValueIn, GetValueOut()));

                // Tally transaction fees
                int64_t nTxFee = nValueIn - GetValueOut();
                if (nTxFee < 0)
                    return DoS(100, error("ConnectInputs() : %s nTxFee < 0", GetHash().ToString().substr(0,10).c_str()));

                unsigned int nSize = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
                // enforce transaction fees for every block
                if (nTxFee < GetMinFee(1000, GMF_RELAY, nSize, true))
                    return fBlock? DoS(100, error("ConnectInputs() : %s not paying required fee=%s, paid=%s", GetHash().ToString().substr(0,10).c_str(), FormatMoney(GetMinFee()).c_str(), FormatMoney(nTxFee).c_str())) : false;

                nFees += nTxFee;
                if (!MoneyRange(nFees))
                    return DoS(100, error("ConnectInputs() : nFees out of range"));
            }
        }
    }

    return true;
}


bool CTransaction::ClientConnectInputs()
{
    if (IsCoinBase())
        return false;

    // Take over previous transactions' spent pointers
    {
        LOCK(mempool.cs);
        int64_t nValueIn = 0;
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            // Get prev tx from single transactions in memory
            COutPoint prevout = vin[i].prevout;
            if (!mempool.exists(prevout.hash))
                return false;
            CTransaction& txPrev = mempool.lookup(prevout.hash);

            if (prevout.n >= txPrev.vout.size())
                return false;

            // Verify signature
            if (!VerifySignature(txPrev, *this, i, 0))
                return error("ConnectInputs() : VerifySignature failed");

            ///// this is redundant with the mempool.mapNextTx stuff,
            ///// not sure which I want to get rid of
            ///// this has to go away now that posNext is gone
            // // Check for conflicts
            // if (!txPrev.vout[prevout.n].posNext.IsNull())
            //     return error("ConnectInputs() : prev tx already used");
            //
            // // Flag outpoints as used
            // txPrev.vout[prevout.n].posNext = posThisTx;

            nValueIn += txPrev.vout[prevout.n].nValue;

            if (!MoneyRange(txPrev.vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                return error("ClientConnectInputs() : txin values out of range");
        }
        if (GetValueOut() > nValueIn)
            return false;
    }

    return true;
}




bool CBlock::DisconnectBlock(CTxDB& txdb, CBlockIndex* pindex)
{
    // Disconnect in reverse order
    for (int i = vtx.size()-1; i >= 0; i--)
        if (!vtx[i].DisconnectInputs(txdb))
            //fixme:LIGHTHYBRID
            if (currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
                return false;

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = 0;
        if (!txdb.WriteBlockIndex(pindex->pprev->GetBlockHash(),blockindexPrev))
            return error("DisconnectBlock() : WriteBlockIndex failed");
    }

    // ppcoin: clean up wallet after disconnecting coinstake
    BOOST_FOREACH(CTransaction& tx, vtx)
        SyncWithWallets(tx, this, false, false);

    return true;
}

bool CBlock::ConnectBlock(CTxDB& txdb, CBlockIndex* pindex, bool fJustCheck, bool fForceFullConnection)
{
    if (currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
        fForceFullConnection = true;

    // Check it again in case a previous version let a bad block in, but skip BlockSig checking
    if (!CheckBlock(!fJustCheck, !fJustCheck, false, fForceFullConnection))
        return false;

    //// issue here: it doesn't know the version
    unsigned int nTxPos;
    if (fJustCheck)
        // FetchInputs treats CDiskTxPos(1,1,1) as a special "refer to memorypool" indicator
        // Since we're just checking the block and not actually connecting it, it might not (and probably shouldn't) be on the disk to get the transaction from
        nTxPos = 1;
    else
        nTxPos = pindex->nBlockPos + ::GetSerializeSize(CBlock(), SER_DISK, CLIENT_VERSION) - (2 * GetSizeOfCompactSize(0)) + GetSizeOfCompactSize(vtx.size());

    map<uint256, CTxIndex> mapQueuedChanges;
    int64_t nFees = 0;
    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    int64_t nStakeReward = 0;
    unsigned int nSigOps = 0;


    BOOST_FOREACH(CTransaction& tx, vtx)
    {
        uint256 hashTx = tx.GetHash();

        // Do not allow blocks that contain transactions which 'overwrite' older transactions,
        // unless those are already completely spent.
        // If such overwrites are allowed, coinbases and transactions depending upon those
        // can be duplicated to remove the ability to spend the first instance -- even after
        // being sent to another address.
        // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
        // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
        // already refuses previously-known transaction ids entirely.
        // This rule was originally applied all blocks whose timestamp was after March 15, 2012, 0:00 UTC.
        // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
        // two in the chain that violate it. This prevents exploiting the issue against nodes in their
        // initial block download.
        CTxIndex txindexOld;
        if (txdb.ReadTxIndex(hashTx, txindexOld))
        {
            BOOST_FOREACH(CDiskTxPos &pos, txindexOld.vSpent)
            {
                if (pos.IsNull())
                {
                    //fixme:LIGHTHYBRID
                    if (fForceFullConnection)
                        return false;
                }
            }
        }

        nSigOps += tx.GetLegacySigOpCount();
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return DoS(100, error("ConnectBlock() : too many sigops"));

        CDiskTxPos posThisTx(pindex->nFile, pindex->nBlockPos, nTxPos);
        if (!fJustCheck)
            nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);

        MapPrevTx mapInputs;
        if (tx.IsCoinBase())
            nValueOut += tx.GetValueOut();
        else
        {
            bool fInvalid;
            //fixme:LIGHTHYBRID
            if (fForceFullConnection)
            {
                if (!tx.FetchInputs(txdb, mapQueuedChanges, true, false, mapInputs, fInvalid))
                    return false;

                // Add in sigops done by pay-to-script-hash inputs;
                // this is to prevent a "rogue miner" from creating
                // an incredibly-expensive-to-validate block.
                nSigOps += tx.GetP2SHSigOpCount(mapInputs);
                if (nSigOps > MAX_BLOCK_SIGOPS)
                    return DoS(100, error("ConnectBlock() : too many sigops"));
            }


            int64_t nTxValueIn = tx.GetValueIn(mapInputs);
            int64_t nTxValueOut = tx.GetValueOut();
            nValueIn += nTxValueIn;
            nValueOut += nTxValueOut;
            if (!tx.IsCoinStake())
                nFees += nTxValueIn - nTxValueOut;
            if (tx.IsCoinStake())
                nStakeReward = nTxValueOut - nTxValueIn;

            if (!tx.ConnectInputs(txdb, mapInputs, mapQueuedChanges, posThisTx, pindex, true, false))
            {
                //fixme:LIGHTHYBRID
                if (fForceFullConnection)
                    return false;
            }
        }

        mapQueuedChanges[hashTx] = CTxIndex(posThisTx, tx.vout.size());
    }

    uint256 prevHash = 0;
    if(pindex->pprev)
    {
        prevHash = pindex->pprev->GetBlockHash();
    }

    if (IsProofOfWork())
    {
        int64_t nReward = GetProofOfWorkReward(pindex->nHeight, nFees, prevHash);
        // Check coinbase reward
        if (fForceFullConnection)
            if (vtx[0].GetValueOut() > nReward)
                return DoS(50, error("ConnectBlock() : coinbase reward exceeded (actual=%" PRId64 " vs calculated=%" PRId64 ")",
                       vtx[0].GetValueOut(),
                       nReward));
    }
    if (IsProofOfStake())
    {
        // ppcoin: coin stake tx earns reward instead of paying fee
        uint64_t nCoinAge;
        if(fForceFullConnection)
            if (!vtx[1].GetCoinAge(txdb, nTime, nCoinAge))
                return error("ConnectBlock() : %s unable to get coin age for coinstake", vtx[1].GetHash().ToString().substr(0,10).c_str());

        int64_t nCalculatedStakeReward = GetProofOfStakeReward(nCoinAge, nFees);
        if(fForceFullConnection)
        {
            if (nStakeReward > nCalculatedStakeReward)
                return DoS(100, error("ConnectBlock() : coinstake pays too much(actual=%" PRId64 " vs calculated=%" PRId64 ")", nStakeReward, nCalculatedStakeReward));
        }
    }

    // ppcoin: track money supply and mint amount info
    pindex->nMint = nValueOut - nValueIn + nFees;
    pindex->nMoneySupply = (pindex->pprev? pindex->pprev->nMoneySupply : 0) + nValueOut - nValueIn;
    if (!txdb.WriteBlockIndex(pindex->GetBlockHash(), CDiskBlockIndex(pindex)))
        return error("Connect() : WriteBlockIndex for pindex failed");

    if (fJustCheck)
        return true;

    // Write queued txindex changes
    for (map<uint256, CTxIndex>::iterator mi = mapQueuedChanges.begin(); mi != mapQueuedChanges.end(); ++mi)
    {
        if (!txdb.UpdateTxIndex((*mi).first, (*mi).second))
            return error("ConnectBlock() : UpdateTxIndex failed");
    }

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = pindex->GetBlockHash();
        if (!txdb.WriteBlockIndex(pindex->pprev->GetBlockHash(), blockindexPrev))
            return error("ConnectBlock() : WriteBlockIndex failed");
    }

    // Watch for transactions paying to me
    BOOST_FOREACH(CTransaction& tx, vtx)
        SyncWithWallets(tx, this, true);

    return true;
}

bool static Reorganize(CTxDB& txdb, CBlockIndex* pindexNew)
{
    //fixme:LIGHTHYBRID
    if(currentClientMode != ClientFull)
    {
        if(currentLoadState == LoadState_SyncHeadersFromEpoch || currentLoadState == LoadState_SyncAllHeaders || currentLoadState == LoadState_CheckPoint)
            return true;
    }


    printf("REORGANIZE\n");

    // Find the fork
    CBlockIndex* pfork = pindexBest;
    CBlockIndex* plonger = pindexNew;
    while (pfork != plonger)
    {
        while (plonger->nHeight > pfork->nHeight)
            if (!(plonger = plonger->pprev))
                return error("Reorganize() : plonger->pprev is null");
        if (pfork == plonger)
            break;
        if (!(pfork = pfork->pprev))
            return error("Reorganize() : pfork->pprev is null");
    }

    // List of what to disconnect
    vector<CBlockIndex*> vDisconnect;
    for (CBlockIndex* pindex = pindexBest; pindex != pfork; pindex = pindex->pprev)
        vDisconnect.push_back(pindex);

    // List of what to connect
    vector<CBlockIndex*> vConnect;
    for (CBlockIndex* pindex = pindexNew; pindex != pfork; pindex = pindex->pprev)
        vConnect.push_back(pindex);
    reverse(vConnect.begin(), vConnect.end());

    printf("REORGANIZE: Disconnect %" PRIszu " blocks; %s..%s\n", vDisconnect.size(), pfork->GetBlockHash().ToString().substr(0,20).c_str(), pindexBest->GetBlockHash().ToString().substr(0,20).c_str());
    printf("REORGANIZE: Connect %" PRIszu " blocks; %s..%s\n", vConnect.size(), pfork->GetBlockHash().ToString().substr(0,20).c_str(), pindexNew->GetBlockHash().ToString().substr(0,20).c_str());

    // Disconnect shorter branch
    vector<CTransaction> vResurrect;
    BOOST_FOREACH(CBlockIndex* pindex, vDisconnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("Reorganize() : ReadFromDisk for disconnect failed");
        if (!block.DisconnectBlock(txdb, pindex))
            return error("Reorganize() : DisconnectBlock %s failed", pindex->GetBlockHash().ToString().substr(0,20).c_str());

        // Queue memory transactions to resurrect
        if (currentClientMode == ClientFull || (currentLoadState == LoadState_AcceptingNewBlocks))
        {
            BOOST_FOREACH(const CTransaction& tx, block.vtx)
                if (!(tx.IsCoinBase() || tx.IsCoinStake()))
                    vResurrect.push_back(tx);
        }
    }

    // Connect longer branch
    vector<CTransaction> vDelete;
    for (unsigned int i = 0; i < vConnect.size(); i++)
    {
        CBlockIndex* pindex = vConnect[i];
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("Reorganize() : ReadFromDisk for connect failed");
        if (!block.ConnectBlock(txdb, pindex))
        {
            // Invalid block
            return error("Reorganize() : ConnectBlock %s failed", pindex->GetBlockHash().ToString().substr(0,20).c_str());
        }

        // Queue memory transactions to delete
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
            vDelete.push_back(tx);
    }
    if (!txdb.WriteHashBestChain(pindexNew->GetBlockHash()))
        return error("Reorganize() : WriteHashBestChain failed");

    // Make sure it's successfully written to disk before changing memory structure
    if (!txdb.TxnCommit())
        return error("Reorganize() : TxnCommit failed");

    // Disconnect shorter branch
    BOOST_FOREACH(CBlockIndex* pindex, vDisconnect)
        if (pindex->pprev)
            pindex->pprev->pnext = NULL;

    // Connect longer branch
    BOOST_FOREACH(CBlockIndex* pindex, vConnect)
        if (pindex->pprev)
            pindex->pprev->pnext = pindex;

    // Resurrect memory transactions that were in the disconnected branch
    BOOST_FOREACH(CTransaction& tx, vResurrect)
        tx.AcceptToMemoryPool(txdb, false);

    // Delete redundant memory transactions that are in the connected branch
    BOOST_FOREACH(CTransaction& tx, vDelete) {
        mempool.remove(tx);
        mempool.removeConflicts(tx);
    }

    printf("REORGANIZE: done\n");

    return true;
}


// Called from inside SetBestChain: attaches a block to the new best chain being built
bool CBlock::SetBestChainInner(CTxDB& txdb, CBlockIndex *pindexNew)
{
    uint256 hash = GetHash();

    // Adding to current best branch
    if (!ConnectBlock(txdb, pindexNew) || !txdb.WriteHashBestChain(hash))
    {
        txdb.TxnAbort();
        InvalidChainFound(pindexNew);
        return false;
    }
    if (!txdb.TxnCommit())
        return error("SetBestChain() : TxnCommit failed");

    // Add to current best branch
    pindexNew->pprev->pnext = pindexNew;

    // Delete redundant memory transactions
    BOOST_FOREACH(CTransaction& tx, vtx)
        mempool.remove(tx);

    return true;
}

bool CBlock::SetBestChain(CTxDB& txdb, CBlockIndex* pindexNew)
{
    uint256 hash = GetHash();

    if (!txdb.TxnBegin())
        return error("SetBestChain() : TxnBegin failed");

    if (pindexGenesisBlock == NULL && hash == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet))
    {
        txdb.WriteHashBestChain(hash);
        if (!txdb.TxnCommit())
            return error("SetBestChain() : TxnCommit failed");
        pindexGenesisBlock = pindexNew;
    }
    else if (hashPrevBlock == hashBestChain)
    {
        if (!SetBestChainInner(txdb, pindexNew))
            return error("SetBestChain() : SetBestChainInner failed");
    }
    else
    {
        // the first block in the new chain that will cause it to become the new best chain
        CBlockIndex *pindexIntermediate = pindexNew;

        // list of blocks that need to be connected afterwards
        std::vector<CBlockIndex*> vpindexSecondary;

        // Reorganize is costly in terms of db load, as it works in a single db transaction.
        // Try to limit how much needs to be done inside
        while (pindexIntermediate->pprev && pindexIntermediate->pprev->nChainTrust > pindexBest->nChainTrust)
        {
            vpindexSecondary.push_back(pindexIntermediate);
            pindexIntermediate = pindexIntermediate->pprev;
        }

        if (!vpindexSecondary.empty())
            printf("Postponing %" PRIszu " reconnects\n", vpindexSecondary.size());

        // Switch to new best branch
        if (!Reorganize(txdb, pindexIntermediate))
        {
            txdb.TxnAbort();
            InvalidChainFound(pindexNew);
            return error("SetBestChain() : Reorganize failed");
        }

        //Have to close as can't have two open at once.
        txdb.TxnAbort();

        // Connect further blocks
        BOOST_REVERSE_FOREACH(CBlockIndex *pindex, vpindexSecondary)
        {
            CBlock block;
            if (!block.ReadFromDisk(pindex))
            {
                printf("SetBestChain() : ReadFromDisk failed\n");
                break;
            }
            if (!txdb.TxnBegin()) {
                printf("SetBestChain() : TxnBegin 2 failed\n");
                break;
            }
            // errors now are not fatal, we still did a reorganisation to a new chain in a valid way
            if (!block.SetBestChainInner(txdb, pindex))
                break;
        }
    }


    // Update best block in wallet (so we can detect restored wallets)
    bool fIsInitialDownload = IsInitialBlockDownload();
    if (!fIsInitialDownload)
    {
        const CBlockLocator locator(pindexNew);
        ::SetBestChain(locator);
    }

    // New best block
    hashBestChain = hash;
    pindexBest = pindexNew;

    pblockindexFBBHLast = NULL;
    nBestHeight = pindexBest->nHeight;
    nBestChainTrust = pindexNew->nChainTrust;
    nTimeBestReceived = GetTime();
    nTransactionsUpdated++;

    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    if ( !fDebug && IsInitialBlockDownload() )
        printf("SetBestChain: %s %d\n", hashBestChain.ToString().substr(0,20).c_str(), nBestHeight);
    else
        printf("SetBestChain: hash=%s height=%d trust=%s blocktrust=%" PRId64 " date=%s\n", hashBestChain.ToString().substr(0,20).c_str(), nBestHeight, CBigNum(nBestChainTrust).ToString().c_str(), nBestBlockTrust.Get64(), DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());

    // Check the version of the last 100 blocks to see if we need to upgrade:
    if (!fIsInitialDownload)
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = pindexBest;
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            printf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, CBlock::CURRENT_VERSION);
        if (nUpgraded > 100/2)
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete, upgrade required!");
    }

    std::string strCmd = GetArg("-blocknotify", "");

    if (!fIsInitialDownload && !strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", hashBestChain.GetHex());
        boost::thread t(runCommand, strCmd); // thread runs free
    }

    return true;
}

// ppcoin: total coin age spent in transaction, in the unit of coin-days.
// Only those coins meeting minimum age requirement counts. As those
// transactions not in main chain are not currently indexed so we
// might not find out about their coin age. Older transactions are
// guaranteed to be in main chain by sync-checkpoint. This rule is
// introduced to help nodes establish a consistent view of the coin
// age (trust score) of competing branches.
bool CTransaction::GetCoinAge(CTxDB& txdb, unsigned int nTxTime, uint64_t& nCoinAge) const
{
    CBigNum bnCentSecond = 0;  // coin age in the unit of cent-seconds
    nCoinAge = 0;

    if (fDebug || !IsInitialBlockDownload())
        printf("GetCoinAge::%s\n", ToString().c_str());

    if (IsCoinBase())
        return true;

    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        // First try finding the previous transaction in database
        CTransaction txPrev;
        CTxIndex txindex;
        if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
            continue;  // previous transaction not in main chain

        // Read block header
        CBlock block, blockCur;
        if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
            return false; // unable to read block of previous transaction

        unsigned int nPrevTime = block.GetBlockTime();
        if (nPrevTime + nStakeMinAge > nTxTime || nTxTime < nPrevTime)
            continue; // only count coins meeting min age requirement

        int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
        bnCentSecond += CBigNum(nValueIn) * (nTxTime - nPrevTime) / CENT;

        if (fDebug && GetBoolArg("-printcoinage"))
            printf("coin age nValueIn=%" PRId64 " nTimeDiff=%d bnCentSecond=%s\n", nValueIn, nTxTime - nPrevTime, bnCentSecond.ToString().c_str());
    }

    CBigNum bnCoinDay = bnCentSecond * CENT / COIN / (24 * 60 * 60);
    if (fDebug && GetBoolArg("-printcoinage"))
        printf("coin age bnCoinDay=%s\n", bnCoinDay.ToString().c_str());
    nCoinAge = bnCoinDay.getuint64();
    return true;
}

// ppcoin: total coin age spent in block, in the unit of coin-days.
bool CBlock::GetCoinAge(uint64_t& nCoinAge) const
{
    nCoinAge = 0;

    CTxDB txdb("r");
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        uint64_t nTxCoinAge;
        if (tx.GetCoinAge(txdb, nTime, nTxCoinAge))
            nCoinAge += nTxCoinAge;
        else
            return false;
    }

    if (nCoinAge == 0) // block coin age minimum 1 coin-day
        nCoinAge = 1;
    if (fDebug && GetBoolArg("-printcoinage"))
        printf("block coin age total nCoinDays=%" PRId64 "\n", nCoinAge);
    return true;
}

bool CBlock::AddToBlockIndex(unsigned int nFile, unsigned int nBlockPos, const uint256& hashProof, bool replacingHeader, bool replacingPlaceholder)
{
    // Check for duplicate
    uint256 hash = GetHash();

    if(!replacingHeader && !replacingPlaceholder)
    {
        if (mapBlockIndex.count(hash))
        {
            return error("AddToBlockIndex() : %s already exists", hash.ToString().substr(0,20).c_str());
        }
    }

    CBlockIndex* pPrevIndex=NULL;
    CBlockIndex* pNextIndex=NULL;
    if (replacingHeader || replacingPlaceholder || headerOnly || placeHolderBlock)
    {
        if(mapBlockIndex.count(hashPrevBlock))
        {
            pPrevIndex = mapBlockIndex[hashPrevBlock];
            pNextIndex = pPrevIndex->pnext;
        }
    }

    CBlockIndex* pindexNew=NULL;
    if (replacingHeader || (replacingPlaceholder && mapBlockIndex[hash]->numPlacesHeld == 1))
    {
        pindexNew = mapBlockIndex[hash];
        pindexNew->Overwrite(nFile, nBlockPos, *this);
    }
    else
    {
        // Construct new block index object
        pindexNew = new CBlockIndex(nFile, nBlockPos, *this);

        if (!pindexNew)
            return error("AddToBlockIndex() : new CBlockIndex failed");
    }
    pindexNew->phashBlock = &hash;


    map<uint256, CBlockIndex*>::iterator miPrev = mapBlockIndex.find(hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        if (currentClientMode != ClientFull && currentLoadState != LoadState_AcceptingNewBlocks && currentLoadState != LoadState_VerifyAllBlocks)
        {
            if((*miPrev).second->pnext && (*miPrev).second->pnext != pindexNew)
            {
                pindexNew->pnext = (*miPrev).second->pnext;
                pindexNew->pnext->pprev = pindexNew;
            }
            pindexNew->pprev = (*miPrev).second;
            pindexNew->pprev->pnext = pindexNew;
        }
        else
        {
            pindexNew->pprev = (*miPrev).second;
        }


        if(placeHolderBlock)
        {
            pindexNew->nHeight = pindexNew->pprev->nHeight + numPlacesHeld -1;
        }
        else
        {
            pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        }
    }

    // ppcoin: compute chain trust score
    if(placeHolderBlock)
    {
        pindexNew->nChainTrust = (pindexNew->pprev ? pindexNew->pprev->nChainTrust : 0) + 1;
    }
    else
    {
        pindexNew->nChainTrust = (pindexNew->pprev ? pindexNew->pprev->nChainTrust : 0) + pindexNew->GetBlockTrust();
    }

    // ppcoin: compute stake entropy bit for stake modifier
    if (!pindexNew->SetStakeEntropyBit(GetStakeEntropyBit()))
        return error("AddToBlockIndex() : SetStakeEntropyBit() failed");

    // Record proof hash value
    pindexNew->hashProof = hashProof;

    // ppcoin: compute stake modifier
    //fixme:LIGHTHYBRID security
    if(currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
    {
        uint64_t nStakeModifier = 0;
        bool fGeneratedStakeModifier = false;
        vector<pair<int64_t, uint256> > vSortedByTimestamp;
        if (!ComputeNextStakeModifier(pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier, vSortedByTimestamp))
            return error("AddToBlockIndex() : ComputeNextStakeModifier() failed");
        pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
        pindexNew->nStakeModifierChecksum = GetStakeModifierChecksum(pindexNew);
        if (!CheckStakeModifierCheckpoints(pindexNew->nHeight, pindexNew->nStakeModifierChecksum))
            return error("AddToBlockIndex() : Rejected by stake modifier checkpoint height=%d, modifier=0x%016" PRIx64, pindexNew->nHeight, nStakeModifier);
    }

    // Add to mapBlockIndex
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    if (pindexNew->IsProofOfStake())
        setStakeSeen.insert(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));
    pindexNew->phashBlock = &((*mi).first);

    if ( currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks)
        pindexNew->SetVerified(true);

    // Write to disk block index
    CTxDB txdb;
    if (!txdb.TxnBegin())
        return false;
    if (pPrevIndex)
        txdb.WriteBlockIndex(pPrevIndex->GetBlockHash(), CDiskBlockIndex(pPrevIndex));
    txdb.WriteBlockIndex(pindexNew->GetBlockHash(), CDiskBlockIndex(pindexNew));


    // If we load a header that comes before a placeholder block than we should reduce the count of that placeholder block by 1.
    // Write to disk new placeholder block...
    if (currentClientMode != ClientFull && ( currentLoadState == LoadState_SyncHeadersFromEpoch || currentLoadState == LoadState_SyncAllHeaders) )
    {
        if(!replacingHeader && !replacingPlaceholder && pNextIndex && pNextIndex->IsPlaceHolderBlock())
        {
            pNextIndex->numPlacesHeld = pNextIndex->numPlacesHeld - 1;
            pNextIndex->pprev = pindexNew;
            pindexNew->pnext = pNextIndex;

            CTxDB txdb;
            txdb.WriteBlockIndex(pNextIndex->GetBlockHash(), CDiskBlockIndex(pNextIndex));
        }
    }

    if (!txdb.TxnCommit())
        return false;

    // New best
    if( currentClientMode == ClientFull || ( currentClientMode == ClientHybrid && ( currentLoadState == LoadState_AcceptingNewBlocks || currentLoadState == LoadState_VerifyAllBlocks ) ) )
    {
        if (pindexNew->nChainTrust > nBestChainTrust)
            if (!SetBestChain(txdb, pindexNew))
                return false;
    }
    else if(!pindexNew->pnext)
    {
        if (!SetBestChain(txdb, pindexNew))
        {
            if(currentLoadState != LoadState_SyncAllHeaders && currentLoadState != LoadState_SyncAllBlocks)
                return false;
        }
    }

    if (pindexNew == pindexBest)
    {
        // Notify UI to display prev block's coinbase if it was ours
        static uint256 hashPrevBestCoinBase;
        UpdatedTransaction(hashPrevBestCoinBase);
        if(currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
            hashPrevBestCoinBase = vtx[0].GetHash();
    }

    uiInterface.NotifyBlocksChanged();
    return true;
}




bool CBlock::CheckBlock(bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig, bool fForceFullCheck) const
{
    if (currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
        fForceFullCheck = true;

    //fixme:LIGHTHYBRIDSECURITY
    if (!fForceFullCheck)
        return true;

    // These are checks that are independent of context
    // that can be verified before saving an orphan block.

    // Size limits
    if (vtx.empty() || vtx.size() > MAX_BLOCK_SIZE || ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CheckBlock() : size limits failed"));

    // Check proof of work matches claimed amount
    if (fCheckPOW && IsProofOfWork() && !CheckProofOfWork(GetPoWHash(), nBits))
        return DoS(50, error("CheckBlock() : proof of work failed"));

    // Check timestamp
    if (GetBlockTime() > FutureDrift(GetAdjustedTime()))
        return error("CheckBlock() : block timestamp too far in the future");

    // First transaction must be coinbase, the rest must not be
    if (vtx.empty() || !vtx[0].IsCoinBase())
        return DoS(100, error("CheckBlock() : first tx is not coinbase"));
    for (unsigned int i = 1; i < vtx.size(); i++)
        if (vtx[i].IsCoinBase())
            return DoS(100, error("CheckBlock() : more than one coinbase"));

    if (IsProofOfStake())
    {
        if(nVersion == 1) return DoS(50, error("CheckBlock() : PoS block version too low!"));

        // Coinbase output should be empty if proof-of-stake block
        if (vtx[0].vout.size() != 1 || !vtx[0].vout[0].IsEmpty())
            return DoS(100, error("CheckBlock() : coinbase output not empty for proof-of-stake block"));

        // Second transaction must be coinstake, the rest must not be
        if (vtx.empty() || !vtx[1].IsCoinStake())
            return DoS(100, error("CheckBlock() : second tx is not coinstake"));
        for (unsigned int i = 2; i < vtx.size(); i++)
            if (vtx[i].IsCoinStake())
                return DoS(100, error("CheckBlock() : more than one coinstake"));

        // NovaCoin: check proof-of-stake block signature
        if (fCheckSig && !CheckBlockSignature())
            return DoS(100, error("CheckBlock() : bad proof-of-stake block signature"));
    }

    // Check transactions
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        if (!tx.CheckTransaction())
            return DoS(tx.nDoS, error("CheckBlock() : CheckTransaction failed"));
    }

    // Check for duplicate txids. This is caught by ConnectInputs(),
    // but catching it earlier avoids a potential DoS attack:
    set<uint256> uniqueTx;
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        uniqueTx.insert(tx.GetHash());
    }
    if (uniqueTx.size() != vtx.size())
        return DoS(100, error("CheckBlock() : duplicate transaction"));

    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        nSigOps += tx.GetLegacySigOpCount();
    }
    if (nSigOps > MAX_BLOCK_SIGOPS)
        return DoS(100, error("CheckBlock() : out-of-bounds SigOpCount"));


    // Check merkle root
    if (fCheckMerkleRoot && hashMerkleRoot != BuildMerkleTree())
        return DoS(100, error("CheckBlock() : hashMerkleRoot mismatch"));

    return true;
}


bool CBlock::AcceptBlock()
{
    if (nVersion > CURRENT_VERSION)
        return DoS(100, error("AcceptBlock() : reject unknown block version %d", nVersion));

    // Check for duplicate
    uint256 hash = GetHash();

    bool replacingHeader=false;
    bool replacingPlaceholder=false;
    if (mapBlockIndex.count(hash))
    {
        if(mapBlockIndex[hash]->IsHeaderOnly() && (currentClientMode != ClientFull))
        {
            replacingHeader=true;
        }
        else if(mapBlockIndex[hash]->IsPlaceHolderBlock() && (currentClientMode != ClientFull))
        {
            replacingPlaceholder=true;
        }
        else
        {
            return error("AcceptBlock() : block already in mapBlockIndex");
        }
    }

    // Get prev block index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashPrevBlock);
    if (mi == mapBlockIndex.end())
        return DoS(10, error("AcceptBlock() : prev block not found"));
    CBlockIndex* pindexPrev = (*mi).second;
    int nHeight = pindexPrev->nHeight+1;

    uint256 hashProof;
    if(!placeHolderBlock && !headerOnly)
    {
        // HARD FORK: switch to version 3 starting from PoS
        if (nHeight > LAST_POW_BLOCK && nVersion < 3)
            return DoS(100, error("AcceptBlock() : reject version <3 block at height %d", nHeight));
        if (nHeight <= LAST_POW_BLOCK && nVersion > 2)
            return DoS(100, error("AcceptBlock() : reject version >2 block at height %d", nHeight));

        if (IsProofOfWork() && nHeight > LAST_POW_BLOCK)
            return DoS(100, error("AcceptBlock() : reject proof-of-work at height %d", nHeight));

        // Check proof-of-work or proof-of-stake
        //fixme:LIGHTHYBRIDSECURITY
        if(currentClientMode == ClientFull || (currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
        {
            if (nBits != GetNextWorkRequired(pindexPrev, IsProofOfStake(), this))
                return DoS(100, error("AcceptBlock() : incorrect %s", IsProofOfWork() ? "proof-of-work" : "proof-of-stake"));
        }

        // HARD FORK: Check timestamp against prev
        // This would have prevented the KGW time warp...
        if (nHeight > LAST_POW_BLOCK &&
          (GetBlockTime() <= pindexPrev->GetPastTimeLimit()
            || FutureDrift(GetBlockTime()) < pindexPrev->GetBlockTime()))
            return error("AcceptBlock() : block's timestamp is too early");

        // Check that all transactions are finalized
        BOOST_FOREACH(const CTransaction& tx, vtx)
        {
            if (!tx.IsFinal(nHeight, GetBlockTime()))
                return DoS(10, error("AcceptBlock() : contains a non-final transaction"));
        }

        // Check that the block chain matches the known block chain up to a checkpoint
        if (!Checkpoints::CheckHardened(nHeight, hash))
            return DoS(100, error("AcceptBlock() : rejected by hardened checkpoint lock-in at %d", nHeight));


        // Verify hash target and signature of coinstake tx
        if (IsProofOfStake())
        {
            uint256 targetProofOfStake;
            if(currentClientMode==ClientFull || (currentClientMode==ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks))
            {
                CTxDB txdb("r");
                if (!CheckProofOfStake(txdb, vtx[1], nTime, nBits, hashProof, targetProofOfStake))
                {
                    printf("WARNING: AcceptBlock(): check proof-of-stake failed for block %s\n", hash.ToString().c_str());
                    return false; // do not error here as we expect this during initial block download
                }
            }
        }
        // PoW is checked in CheckBlock()
        if (IsProofOfWork())
        {
            hashProof = GetPoWHash();
        }

        if(currentClientMode == ClientFull || currentLoadState == LoadState_AcceptingNewBlocks || currentLoadState == LoadState_VerifyAllBlocks)
        {
            bool cpSatisfies = Checkpoints::CheckSync(hash, pindexPrev);

            // Check that the block satisfies synchronized checkpoint
            if (CheckpointsMode == Checkpoints::STRICT && !cpSatisfies)
                return error("AcceptBlock() : rejected by synchronized checkpoint");

            if (CheckpointsMode == Checkpoints::ADVISORY && !cpSatisfies)
                strMiscWarning = _("WARNING: syncronized checkpoint violation detected, but skipped!");
        }

        //fixme:LIGHTHYBRID check if this test can be improved
        if (currentClientMode == ClientFull  && (currentLoadState == LoadState_AcceptingNewBlocks || currentLoadState == LoadState_VerifyAllBlocks))
        {
            // Enforce rule that the coinbase starts with serialized block height
            // Unfortunately there are bad version 2 blocks in Pandacoin blockchain (e.g. block 6)
            if ( (nVersion > 2 || (!fTestNet && nVersion ==2) ) && nHeight > 1000)
            {
                CScript expect = CScript() << nHeight;
                if (vtx[0].vin[0].scriptSig.size() < expect.size() ||
                    !std::equal(expect.begin(), expect.end(), vtx[0].vin[0].scriptSig.begin()))
                    return DoS(100, error("AcceptBlock() : block height mismatch in coinbase"));
            }
        }
    }

    // Write block to history file
    if (!CheckDiskSpace(::GetSerializeSize(*this, SER_DISK, CLIENT_VERSION)))
        return error("AcceptBlock() : out of disk space");
    unsigned int nFile = -1;
    unsigned int nBlockPos = 0;

    //fixme:LIGHTHYBRID Lame - when replacing header or placeholder we leave the old stuff there as well (no way to delete from blockstore)...
    if (!WriteToDisk(nFile, nBlockPos))
        return error("AcceptBlock() : WriteToDisk failed");
    if (!AddToBlockIndex(nFile, nBlockPos, hashProof , replacingHeader, replacingPlaceholder))
        return error("AcceptBlock() : AddToBlockIndex failed");

    // Relay inventory, but don't relay old inventory during initial block download
    int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
    if (hashBestChain == hash)
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if (nBestHeight > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                pnode->PushInventory(CInv(MSG_BLOCK, hash));
    }

    // ppcoin: check pending sync-checkpoint
    Checkpoints::AcceptPendingSyncCheckpoint();

    return true;
}

uint256 CBlockIndex::GetBlockTrust() const
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    if (bnTarget <= 0)
        return 0;

    return ((CBigNum(1)<<256) / (bnTarget+1)).getuint256();
}

bool CBlockIndex::IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned int nRequired, unsigned int nToCheck)
{
    unsigned int nFound = 0;
    for (unsigned int i = 0; i < nToCheck && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}

bool ProcessBlock(CNode* pfrom, CBlock* pblock)
{
    // Check for duplicate
    uint256 hash = pblock->GetHash();
    bool replacingHeader=false;
    bool replacingPlaceholder=false;

    std::map<uint256, CBlockIndex*>::iterator iter = mapBlockIndex.find(hash);
    if(iter != mapBlockIndex.end())
    {
        CBlockIndex* pIndex = iter->second;
        if(pIndex->IsHeaderOnly() && (currentClientMode != ClientFull))
        {
            replacingHeader=true;
        }
        else if(pIndex && pIndex->IsPlaceHolderBlock() && (currentClientMode != ClientFull))
        {
            replacingPlaceholder=true;
        }
        else
        {
            return error("ProcessBlock() : already have block %d %s", mapBlockIndex[hash]->nHeight, hash.ToString().substr(0,20).c_str());
        }
    }

    if (mapOrphanBlocks.count(hash))
        return error("ProcessBlock() : already have block (orphan) %s", hash.ToString().substr(0,20).c_str());

    // ppcoin: check proof-of-stake
    // Limited duplicity on stake: prevents block flood attack
    // Duplicate stake allowed only when there is orphan child block
    if (pblock->IsProofOfStake() && setStakeSeen.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash) && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
        return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for block %s", pblock->GetProofOfStake().first.ToString().c_str(), pblock->GetProofOfStake().second, hash.ToString().c_str());

    // Preliminary checks
    if(!pblock->headerOnly && !pblock->placeHolderBlock)
    {
        if (!pblock->CheckBlock())
            return error("ProcessBlock() : CheckBlock FAILED");


        if(currentLoadState != LoadState_SyncBlocksFromEpoch)
        {
            CBlockIndex* pcheckpoint = Checkpoints::GetLastSyncCheckpoint();
            if (pcheckpoint && (pcheckpoint->GetBlockHash() != hashGenesisBlock) && pblock->hashPrevBlock != hashBestChain && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
            {
                // Extra checks to prevent "fill up memory by spamming with bogus blocks"
                int64_t deltaTime = pblock->GetBlockTime() - pcheckpoint->nTime;
                CBigNum bnNewBlock;
                bnNewBlock.SetCompact(pblock->nBits);
                CBigNum bnRequired;

                if (pblock->IsProofOfStake())
                    bnRequired.SetCompact(ComputeMinStake(GetLastBlockIndex(pcheckpoint, true)->nBits, deltaTime, pblock->nTime));
                else
                    bnRequired.SetCompact(ComputeMinWork(GetLastBlockIndex(pcheckpoint, false)->nBits, deltaTime));

                if (bnNewBlock > bnRequired)
                {
                    if (pfrom)
                        pfrom->Misbehaving(100);
                    return error("ProcessBlock() : block with too little %s", pblock->IsProofOfStake()? "proof-of-stake" : "proof-of-work");
                }
            }
        }
    }

    if(!pblock->headerOnly && !pblock->placeHolderBlock)
    {
        // ppcoin: ask for pending sync-checkpoint if any
        if (!IsInitialBlockDownload())
            Checkpoints::AskForPendingSyncCheckpoint(pfrom);
    }

    // If don't already have its previous block, shunt it off to holding area until we get it
    if (!mapBlockIndex.count(pblock->hashPrevBlock))
    {
        printf("ProcessBlock: ORPHAN BLOCK, prev=%s\n", pblock->hashPrevBlock.ToString().substr(0,20).c_str());
        CBlock* pblock2 = new CBlock(*pblock);
        // ppcoin: check proof-of-stake
        if (pblock2->IsProofOfStake())
        {
            // Limited duplicity on stake: prevents block flood attack
            // Duplicate stake allowed only when there is orphan child block
            if (setStakeSeenOrphan.count(pblock2->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash) && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
                return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for orphan block %s", pblock2->GetProofOfStake().first.ToString().c_str(), pblock2->GetProofOfStake().second, hash.ToString().c_str());
            else
                setStakeSeenOrphan.insert(pblock2->GetProofOfStake());
        }
        mapOrphanBlocks.insert(make_pair(hash, pblock2));
        mapOrphanBlocksByPrev.insert(make_pair(pblock2->hashPrevBlock, pblock2));

        if(currentClientMode == ClientFull || currentLoadState == LoadState_AcceptingNewBlocks)
        {
            // Ask this guy to fill in what we're missing
            if (pfrom)
            {
                pfrom->PushGetBlocks(pindexBest, GetOrphanRoot(pblock2));
                // ppcoin: getblocks may not obtain the ancestor block rejected
                // earlier by duplicate-stake check so we ask for it again directly
                if (!IsInitialBlockDownload())
                    pfrom->AskFor(CInv(MSG_BLOCK, WantedByOrphan(pblock2)));
            }
        }
        return true;
    }

    // Store to disk - and insert into chain
    if (!pblock->AcceptBlock())
        return error("ProcessBlock() : AcceptBlock FAILED");

    if(!replacingPlaceholder && !replacingHeader)
    {
        // Recursively process any orphan blocks that depended on this one
        vector<uint256> vWorkQueue;
        vWorkQueue.push_back(hash);
        for (unsigned int i = 0; i < vWorkQueue.size(); i++)
        {
            uint256 hashPrev = vWorkQueue[i];
            for (multimap<uint256, CBlock*>::iterator mi = mapOrphanBlocksByPrev.lower_bound(hashPrev);
                 mi != mapOrphanBlocksByPrev.upper_bound(hashPrev);
                 ++mi)
            {
                CBlock* pblockOrphan = (*mi).second;
                if (pblockOrphan->AcceptBlock())
                    vWorkQueue.push_back(pblockOrphan->GetHash());
                mapOrphanBlocks.erase(pblockOrphan->GetHash());
                setStakeSeenOrphan.erase(pblockOrphan->GetProofOfStake());
                delete pblockOrphan;
            }
            mapOrphanBlocksByPrev.erase(hashPrev);
        }
    }

    if (fDebug || !IsInitialBlockDownload())
        printf("ProcessBlock: ACCEPTED\n");

    // ppcoin: if responsible for sync-checkpoint send it
    if (pfrom && !CSyncCheckpoint::strMasterPrivKey.empty())
        Checkpoints::SendSyncCheckpoint(Checkpoints::AutoSelectSyncCheckpoint());

    return true;
}

// novacoin: attempt to generate suitable proof-of-stake
bool CBlock::SignBlock(CWallet& wallet, int64_t nFees)
{
    // if we are trying to sign
    //    something except proof-of-stake block template
    if (!vtx[0].vout[0].IsEmpty())
        return false;

    // if we are trying to sign
    //    a complete proof-of-stake block
    if (IsProofOfStake())
        return true;

    static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // startup timestamp

    CKey key;
    CTransaction txCoinStake;
    int64_t nSearchTime = nTime; // search to current time
    unsigned int nTxTime = nTime;

    if (nSearchTime > nLastCoinStakeSearchTime)
    {
        if (wallet.CreateCoinStake(wallet, nBits, nSearchTime-nLastCoinStakeSearchTime, nFees, txCoinStake, nTxTime, key))
        {
            if (nTime >= max(pindexBest->GetPastTimeLimit()+1, PastDrift(pindexBest->GetBlockTime())))
            {
                // Pandacoin: since I've had to get rid of CTransaction's nTime,
                // it's no longer possible to alter the nTime to fit the past block drift
                nTime = nTxTime;

                vtx.insert(vtx.begin() + 1, txCoinStake);
                hashMerkleRoot = BuildMerkleTree();

                // append a signature to our block
                return key.Sign(GetHash(), vchBlockSig);
            }
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }

    return false;
}

bool CBlock::CheckBlockSignature() const
{
    if (IsProofOfWork())
        return vchBlockSig.empty();

    vector<valtype> vSolutions;
    txnouttype whichType;

    const CTxOut& txout = vtx[1].vout[1];

    if (!Solver(txout.scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        valtype& vchPubKey = vSolutions[0];
        CKey key;
        if (!key.SetPubKey(vchPubKey))
            return false;
        if (vchBlockSig.empty())
            return false;
        return key.Verify(GetHash(), vchBlockSig);
    }

    return false;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    {
        fShutdown = true;
        string strMessage = _("Warning: Disk space is low!");
        strMiscWarning = strMessage;
        printf("*** %s\n", strMessage.c_str());
        uiInterface.ThreadSafeMessageBox(strMessage, "Pandacoin", CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION | CClientUIInterface::MODAL);
        StartShutdown();
        return false;
    }
    return true;
}

static filesystem::path BlockFilePath(unsigned int nFile)
{
    string strBlockFn = strprintf("blk%04u.dat", nFile);
    return GetDataDir() / strBlockFn;
}

FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode)
{
    if ((nFile < 1) || (nFile == (unsigned int) -1))
        return NULL;
    FILE* file = fopen(BlockFilePath(nFile).string().c_str(), pszMode);
    if (!file)
        return NULL;
    if (nBlockPos != 0 && !strchr(pszMode, 'a') && !strchr(pszMode, 'w'))
    {
        if (fseek(file, nBlockPos, SEEK_SET) != 0)
        {
            fclose(file);
            return NULL;
        }
    }
    return file;
}

static unsigned int nCurrentBlockFile = 1;
static FILE* prevHandle=NULL;

void FlushBlockFile()
{
    if(prevHandle)
    {
        LOCK(cs_IO);
        FileCommit(prevHandle);
        fclose(prevHandle);
        prevHandle=NULL;
    }
}

void DeleteBlockFile()
{
    for( unsigned int i=0; i <= nCurrentBlockFile; i++ )
    {
        remove(BlockFilePath(i).string().c_str());
    }
    nCurrentBlockFile = 1;
}

FILE* AppendBlockFile(unsigned int& nFileRet)
{
    LOCK(cs_IO);

    nFileRet = 0;
    while (true)
    {
        FILE* file;
        if ( currentClientMode != ClientFull && IsInitialBlockDownload() && prevHandle != NULL )
        {
            file=prevHandle;
        }
        else
        {
            FlushBlockFile();
            file = OpenBlockFile(nCurrentBlockFile, 0, "ab");
            if (!file)
                return NULL;
            if (fseek(file, 0, SEEK_END) != 0)
                return NULL;
            if ( currentClientMode != ClientFull && IsInitialBlockDownload() )
                prevHandle=file;
        }
        // FAT32 file size max 4GB, fseek and ftell max 2GB, so we must stay under 2GB
        if (ftell(file) < (long)(0x7F000000 - MAX_SIZE))
        {
            nFileRet = nCurrentBlockFile;
            return file;
        }
        fclose(file);
        prevHandle=NULL;
        nCurrentBlockFile++;
    }
}

bool CreateGenesisBlock(bool fAllowNew)
{
    if (mapBlockIndex.empty())
    {
        fNewBlockChain = true;

        if (!fAllowNew)
            return false;

        // Genesis block for Pandacoin

        const char* pszTimestamp = "Don't trust Wolong";
        CTransaction txNew;

        txNew.nVersion = 1;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CBigNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 88 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;

        CBlock block;
        block.vtx.push_back(txNew);
        block.hashPrevBlock = 0;
        block.hashMerkleRoot = block.BuildMerkleTree();

        block.nVersion = 1;
        block.nTime    = 1392488611;
        block.nBits    = 0x1e0ffff0;
        block.nNonce   = 1541569;

        block.print();
        assert(block.hashMerkleRoot == uint256("0xfbc9541b2da3a32b93a2bc03feafb5b094abc7a080a37838127765df41dc65fa"));
        assert(block.GetHash() == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet));
        assert(block.CheckBlock());

        // Start new block file
        unsigned int nFile;
        unsigned int nBlockPos;
        if (!block.WriteToDisk(nFile, nBlockPos))
            return error("LoadBlockIndex() : writing genesis block to disk failed");
        if (!block.AddToBlockIndex(nFile, nBlockPos, hashGenesisBlock, false, false))
            return error("LoadBlockIndex() : genesis block not accepted");

        // ppcoin: initialize synchronized checkpoint
        if (!Checkpoints::WriteSyncCheckpoint((!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet)))
            return error("LoadBlockIndex() : failed to init sync checkpoint");
    }
    return true;
}

bool LoadBlockIndex(bool fAllowNew)
{
    CBigNum bnTrustedModulus;

    if (fTestNet)
    {
        pchMessageStart[0] = 0xfc;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0xb7;
        pchMessageStart[3] = 0xdc;
        bnTrustedModulus.SetHex("f0d14cf72623dacfe738d0892b599be0f31052239cddd95a3f25101c801dc990453b38c9434efe3f372db39a32c2bb44cbaea72d62c8931fa785b0ec44531308df3e46069be5573e49bb29f4d479bfc3d162f57a5965db03810be7636da265bfced9c01a6b0296c77910ebdc8016f70174f0f18a57b3b971ac43a934c6aedbc5c866764a3622b5b7e3f9832b8b3f133c849dbcc0396588abcd1e41048555746e4823fb8aba5b3d23692c6857fccce733d6bb6ec1d5ea0afafecea14a0f6f798b6b27f77dc989c557795cc39a0940ef6bb29a7fc84135193a55bcfc2f01dd73efad1b69f45a55198bd0e6bef4d338e452f6a420f1ae2b1167b923f76633ab6e55");
        nStakeMinAge = 1 * 60;
        nCoinbaseMaturity = 5;
    }
    else
    {
        bnTrustedModulus.SetHex("c42403a619d1103f1fd6e9641be966a2ae9daaaeb1f80ed0b7ed3b19b014978b54a2acd64befa03cfca3bab52da43728759db6f36010ab10eadaa63252b8d27d64abfc61bc8bf00fa6c64b172f4b055a58e85854a53dc8629ca2f689dfcd3c0e55daad75b5c9842a54f0bd3a0792d778c080fe59a1c88860659cbc814614262fac26df5445c0150fc48fd91ae9c369704f0d699dc56302287835007fdc82d3f66b1d739530eb2bcdb45057917166d9ff7b97c94bdb43ad35b09dbb712fcebd334a4557fe1c33530531bda6085ad4bfea7fc8d7c902d55c790efcaa992a4812edcbf9e02664c1b0e9077b8b6d9e5ddde197de52a047ce9b44384509025cc9b88d950f8e4443d6c97a93c26472df2a947469e2e115fa5b3d7b8ddaddc3a0a3ab3230bfb3fa3c745982b1376d6c5dacd6112bf3ec496c41e8c0798929d83d9f23bc4c2f5c971fc77bcea46c31d129c745f3106abdf77c7b5288b133a6d228758db2b34f40133648c4bdd6c4780caadbde04445330976e7ba32b56c9f6b438c6d375");
    }

#if 0
    // Set up the Zerocoin Params object
    ZCParams = new libzerocoin::Params(bnTrustedModulus);
#endif

    //
    // Load block index
    //
    CTxDB txdb("cr+");
    if (!txdb.LoadBlockIndex())
        return false;

    //
    // Init with genesis block
    //
    if (!CreateGenesisBlock(fAllowNew))
        return false;

    string strPubKey = "";

    if( currentClientMode == ClientFull || ( currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks) )
    {
        // if checkpoint master key changed must reset sync-checkpoint
        if (!txdb.ReadCheckpointPubKey(strPubKey) || strPubKey != CSyncCheckpoint::strMasterPubKey)
        {
            // write checkpoint master key to db
            txdb.TxnBegin();
            if (!txdb.WriteCheckpointPubKey(CSyncCheckpoint::strMasterPubKey))
                return error("LoadBlockIndex() : failed to write new checkpoint master key to db");
            if (!txdb.TxnCommit())
                return error("LoadBlockIndex() : failed to commit new checkpoint master key to db");
            if ((!fTestNet) && !Checkpoints::ResetSyncCheckpoint())
                return error("LoadBlockIndex() : failed to reset sync-checkpoint");
        }
    }

    return true;
}



void PrintBlockTree()
{
    // pre-compute tree structure
    map<CBlockIndex*, vector<CBlockIndex*> > mapNext;
    for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
    {
        CBlockIndex* pindex = (*mi).second;
        mapNext[pindex->pprev].push_back(pindex);
        // test
        //while (rand() % 3 == 0)
        //    mapNext[pindex->pprev].push_back(pindex);
    }

    vector<pair<int, CBlockIndex*> > vStack;
    vStack.push_back(make_pair(0, pindexGenesisBlock));

    int nPrevCol = 0;
    while (!vStack.empty())
    {
        int nCol = vStack.back().first;
        CBlockIndex* pindex = vStack.back().second;
        vStack.pop_back();

        // print split or gap
        if (nCol > nPrevCol)
        {
            for (int i = 0; i < nCol-1; i++)
                printf("| ");
            printf("|\\\n");
        }
        else if (nCol < nPrevCol)
        {
            for (int i = 0; i < nCol; i++)
                printf("| ");
            printf("|\n");
       }
        nPrevCol = nCol;

        // print columns
        for (int i = 0; i < nCol; i++)
            printf("| ");

        // print item
        CBlock block;
        block.ReadFromDisk(pindex);
        printf("%d (%u,%u) %s  %08x  %s  mint %7s  tx %" PRIszu "",
            pindex->nHeight,
            pindex->nFile,
            pindex->nBlockPos,
            block.GetHash().ToString().c_str(),
            block.nBits,
            DateTimeStrFormat("%x %H:%M:%S", block.GetBlockTime()).c_str(),
            FormatMoney(pindex->nMint).c_str(),
            block.vtx.size());

        PrintWallets(block);

        // put the main time-chain first
        vector<CBlockIndex*>& vNext = mapNext[pindex];
        for (unsigned int i = 0; i < vNext.size(); i++)
        {
            if (vNext[i]->pnext)
            {
                swap(vNext[0], vNext[i]);
                break;
            }
        }

        // iterate children
        for (unsigned int i = 0; i < vNext.size(); i++)
            vStack.push_back(make_pair(nCol+i, vNext[i]));
    }
}

bool LoadExternalBlockFile(FILE* fileIn)
{
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    {
        LOCK(cs_main);
        try {
            CAutoFile blkdat(fileIn, SER_DISK, CLIENT_VERSION);
            unsigned int nPos = 0;
            while (nPos != (unsigned int)-1 && blkdat.good() && !fRequestShutdown)
            {
                unsigned char pchData[65536];
                do {
                    fseek(blkdat, nPos, SEEK_SET);
                    int nRead = fread(pchData, 1, sizeof(pchData), blkdat);
                    if (nRead <= 8)
                    {
                        nPos = (unsigned int)-1;
                        break;
                    }
                    void* nFind = memchr(pchData, pchMessageStart[0], nRead+1-sizeof(pchMessageStart));
                    if (nFind)
                    {
                        if (memcmp(nFind, pchMessageStart, sizeof(pchMessageStart))==0)
                        {
                            nPos += ((unsigned char*)nFind - pchData) + sizeof(pchMessageStart);
                            break;
                        }
                        nPos += ((unsigned char*)nFind - pchData) + 1;
                    }
                    else
                        nPos += sizeof(pchData) - sizeof(pchMessageStart) + 1;
                } while(!fRequestShutdown);
                if (nPos == (unsigned int)-1)
                    break;
                fseek(blkdat, nPos, SEEK_SET);
                unsigned int nSize;
                blkdat >> nSize;
                if (nSize > 0 && nSize <= MAX_BLOCK_SIZE)
                {
                    CBlock block;
                    blkdat >> block;
                    if (ProcessBlock(NULL,&block))
                    {
                        nLoaded++;
                        nPos += 4 + nSize;
                    }
                }
            }
        }
        catch (std::exception &e) {
            printf("%s() : Deserialize or I/O error caught during load\n",
                   __PRETTY_FUNCTION__);
        }
    }
    printf("Loaded %i blocks from external file in %" PRId64 "ms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

extern map<uint256, CAlert> mapAlerts;
extern CCriticalSection cs_mapAlerts;

string GetWarnings(string strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;

    if (GetBoolArg("-testsafemode"))
        strRPC = "test";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strMiscWarning;
    }

    // if detected invalid checkpoint enter safe mode
    if (Checkpoints::hashInvalidCheckpoint != 0)
    {
        nPriority = 3000;
        strStatusBar = strRPC = _("WARNING: Invalid checkpoint found! Displayed transactions may not be correct! You may need to upgrade, or notify developers.");
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = alert.strStatusBar;
                if (nPriority > 1000)
                    strRPC = strStatusBar;
            }
        }
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings() : invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(CTxDB& txdb, const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
        bool txInMap = false;
            {
            LOCK(mempool.cs);
            txInMap = (mempool.exists(inv.hash));
            }
        return txInMap ||
               mapOrphanTransactions.count(inv.hash) ||
               txdb.ContainsTx(inv.hash);
        }

    case MSG_BLOCK:

        std::map<uint256, CBlockIndex*>::iterator iter = mapBlockIndex.find(inv.hash);
        if(iter != mapBlockIndex.end())
        {
            CBlockIndex* pBlock = iter->second;
            if(!pBlock->IsHeaderOnly() && !pBlock->IsPlaceHolderBlock())
                return true;
        }
        std::map<uint256, CBlock*>::iterator orphanIter = mapOrphanBlocks.find(inv.hash);
        if(orphanIter != mapOrphanBlocks.end())
        {
            CBlock* pBlock = orphanIter->second;
            if(!pBlock->headerOnly && !pBlock->placeHolderBlock)
                return true;
        }
        return false;
    }
    // Don't know what it is, just say we already got one
    return true;
}


// The message start string is designed to be unlikely to occur in normal data.
// The characters are rarely used upper ASCII, not valid as UTF-8, and produce
// a large 4-byte int at any alignment.
unsigned char pchMessageStart[4] = { 0xc0, 0xc0, 0xc0, 0xc0 };

bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv)
{
    static map<CService, CPubKey> mapReuseKey;
    RandAddSeedPerfmon();
    if (fDebug)
        printf("received: %s (%" PRIszu " bytes)\n", strCommand.c_str(), vRecv.size());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        printf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }


    if ( ForceTransitionOnNextNodeActivity )
    {
        ForceTransitionOnNextNodeActivity = false;
        TransitionLoadState(pfrom);
    }

    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->Misbehaving(1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (pfrom->nVersion < MIN_PROTO_VERSION)
        {
            // Since February 20, 2012, the protocol is initiated at version 209,
            // and earlier versions are no longer supported
            printf("partner %s using obsolete version %i; disconnecting\n", pfrom->addr.ToString().c_str(), pfrom->nVersion);
            pfrom->fDisconnect = true;
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty())
            vRecv >> pfrom->strSubVer;
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;

        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            pfrom->addrLocal = addrMe;
            SeenLocal(addrMe);
        }

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            printf("connected to self at %s, disconnecting\n", pfrom->addr.ToString().c_str());
            pfrom->fDisconnect = true;
            return true;
        }

        // record my external IP reported by peer
        if (addrFrom.IsRoutable() && addrMe.IsRoutable())
            addrSeenByPeer = addrMe;

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        if (GetBoolArg("-synctime", true))
            AddTimeData(pfrom->addr, nTime);

        // Change version
        pfrom->PushMessage("verack");
        pfrom->vSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (!fNoListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                    pfrom->PushAddress(addr);
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage("getaddr");
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        if(currentClientMode == ClientFull)
        {
            // Ask the first connected node for block updates
            static int nAskedForBlocks = 0;
            if (!pfrom->fClient && !pfrom->fOneShot &&
                (pfrom->nStartingHeight > (nBestHeight - 144)) &&
                (pfrom->nVersion < NOBLKS_VERSION_START ||
                 pfrom->nVersion >= NOBLKS_VERSION_END) &&
                (nAskedForBlocks < 1 || vNodes.size() <= 1))
                {
                    nAskedForBlocks++;
                    pfrom->PushGetBlocks(pindexBest, uint256(0));
                }
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
                item.second.RelayTo(pfrom);
        }

        // Relay sync-checkpoint
        {
            LOCK(Checkpoints::cs_hashSyncCheckpoint);
            if (!Checkpoints::checkpointMessage.IsNull())
                Checkpoints::checkpointMessage.RelayTo(pfrom);
        }

        pfrom->fSuccessfullyConnected = true;

        printf("receive version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n", pfrom->nVersion, pfrom->nStartingHeight, addrMe.ToString().c_str(), addrFrom.ToString().c_str(), pfrom->addr.ToString().c_str());

        cPeerBlockCounts.input(pfrom->nStartingHeight);

        // ppcoin: ask for pending sync-checkpoint if any
        //fixme:LIGHTHYBRIDSECURITY
        if ( currentClientMode == ClientFull || ( currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks ) )
        {
            if (!IsInitialBlockDownload())
                Checkpoints::AskForPendingSyncCheckpoint(pfrom);
        }
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        pfrom->Misbehaving(1);
        return false;
    }


    else if (strCommand == "verack")
    {
        pfrom->vRecv.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        // Load the last checkpoint block
        if (!pfrom->fClient && !pfrom->fOneShot && (pfrom->nStartingHeight > (nBestHeight - 144)) && (pfrom->nVersion < NOBLKS_VERSION_START || pfrom->nVersion >= NOBLKS_VERSION_END) )
        {
            if (currentLoadState == LoadState_Connect)
            {
                // Begin the sync.
                TransitionLoadState(pfrom);
            }
            else if ( currentLoadState == LoadState_AcceptingNewBlocks )
            {
                // Catch up any blocks we missed while disconnected.
                PushGetBlocksFromHash(pfrom, pindexBest->GetBlockHash(), uint256(0));
            }
        }

        if ( currentClientMode != ClientFull )
        {
            // Pick up any pooled work that is already waiting for us (if e.g. doing a sync and a previous node disconnected - or there is more work than nodes)
            PushWork(pfrom);
        }
    }


    else if (strCommand == "addr")
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            pfrom->Misbehaving(20);
            return error("message addr size() = %" PRIszu "", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        BOOST_FOREACH(CAddress& addr, vAddr)
        {
            if (fShutdown)
                return true;
            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the setAddrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = hashSalt ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = hashRand ^ nPointer;
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == "inv")
    {
        //Don't start processing INV messages until we are ready
        if ( currentLoadState == LoadState_CheckPoint || currentLoadState == LoadState_Exiting )
            return true;

        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message inv size() = %" PRIszu "", vInv.size());
        }

        // find last block in inv vector
        unsigned int nLastBlock = (unsigned int)(-1);
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            if (vInv[vInv.size() - 1 - nInv].type == MSG_BLOCK) {
                nLastBlock = vInv.size() - 1 - nInv;
                break;
            }
        }
        CTxDB txdb("r");
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            if (fShutdown)
                return true;

            pfrom->AddInventoryKnown(inv);

            // During header sync don't add to known list as we want to fetch these later still.
            if(inv.type == MSG_BLOCK && currentLoadState == LoadState_SyncHeadersFromEpoch)
            {
                AdditionalBlocksToFetch.insert(inv.hash);
                continue;
            }

            bool fAlreadyHave = AlreadyHave(txdb, inv);
            if (fDebug)
                printf("  got inventory: %s  %s\n", inv.ToString().c_str(), fAlreadyHave ? "have" : "new");

            if (!fAlreadyHave)
            {
                pfrom->AskFor(inv);
            }
            else if (inv.type == MSG_BLOCK && mapOrphanBlocks.count(inv.hash))
            {
                pfrom->PushGetBlocks(pindexBest, GetOrphanRoot(mapOrphanBlocks[inv.hash]));
            }
            else if (nInv == nLastBlock)
            {
                if(currentClientMode == ClientFull || currentLoadState == LoadState_AcceptingNewBlocks)
                {
                    // In case we are on a very long side-chain, it is possible that we already have
                    // the last block in an inv bundle sent in response to getblocks. Try to detect
                    // this situation and push another getblocks to continue.
                    pfrom->PushGetBlocks(mapBlockIndex[inv.hash], uint256(0));
                    if (fDebug)
                        printf("force request: %s\n", inv.ToString().c_str());
                }
            }

            // Track requests for our stuff
            Inventory(inv.hash);
        }

        if(currentClientMode != ClientFull)
        {
            // Pick up any pooled work that is already waiting for us (if e.g. doing a sync and a previous node disconnected - or there is more work than nodes)
            PushWork(pfrom);
        }
    }


    else if (strCommand == "getdata")
    {
        // If we are a light client we can't assist people with these requests so just return.
        //fixme:LIGHTHYBRID
        if(currentClientMode == ClientLight || ( currentClientMode == ClientHybrid && currentLoadState != LoadState_AcceptingNewBlocks ))
            return true;

        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message getdata size() = %" PRIszu "", vInv.size());
        }

        if (fDebugNet || (vInv.size() != 1))
            printf("[%s] received getdata (%" PRIszu " invsz)\n", pfrom->addrName.c_str(), vInv.size());

        BOOST_FOREACH(const CInv& inv, vInv)
        {
            if (fShutdown)
                return true;
            if (fDebugNet || (vInv.size() == 1))
                printf("[%s] received getdata for: %s\n", pfrom->addrName.c_str(), inv.ToString().c_str());

            if (inv.type == MSG_BLOCK)
            {
                // Send block from disk
                map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    CBlock block;
                    block.ReadFromDisk((*mi).second);
                    pfrom->PushMessage("block", block);

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // ppcoin: send latest proof-of-work block to allow the
                        // download node to accept as orphan (proof-of-stake
                        // block might be rejected by stake connection check)
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, GetLastBlockIndex(pindexBest, false)->GetBlockHash()));
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue = 0;
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_TX) {
                    LOCK(mempool.cs);
                    if (mempool.exists(inv.hash)) {
                        CTransaction tx = mempool.lookup(inv.hash);
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        pfrom->PushMessage("tx", ss);
                    }
                }
            }

            // Track requests for our stuff
            Inventory(inv.hash);
        }
    }


    else if (strCommand == "getblocks")
    {
        // If we are a light client we can't assist people with these requests so just return.
        //fixme:LIGHTHYBRID
        if(currentClientMode == ClientLight || ( currentClientMode == ClientHybrid && currentLoadState != LoadState_AcceptingNewBlocks ) || denyIncomingBlocks)
            return true;

        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = locator.GetBlockIndex();

        // Send the rest of the chain
        if (pindex)
            pindex = pindex->pnext;
        int nLimit = MAX_BLOCKS_PER_INV;
        printf("getblocks %d to %s limit %d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString().substr(0,20).c_str(), nLimit);
        for (; pindex; pindex = pindex->pnext)
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                printf("  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString().substr(0,20).c_str());
                // ppcoin: tell downloading node about the latest block if it's
                // without risk being rejected due to stake connection check
                if (hashStop != hashBestChain && pindex->GetBlockTime() + nStakeMinAge > pindexBest->GetBlockTime())
                    pfrom->PushInventory(CInv(MSG_BLOCK, hashBestChain));
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                printf("  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString().substr(0,20).c_str());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }
    else if (strCommand == "checkpoint")
    {
        CSyncCheckpoint checkpoint;
        vRecv >> checkpoint;

        // Light/Hybrid delay until main chain formed
        if (currentClientMode == ClientFull || ( currentClientMode == ClientHybrid && currentLoadState == LoadState_AcceptingNewBlocks ) )
        {
            if (checkpoint.ProcessSyncCheckpoint(pfrom))
            {
                // Relay
                pfrom->hashCheckpointKnown = checkpoint.hashCheckpoint;
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                    checkpoint.RelayTo(pnode);
            }
        }
    }

    else if (strCommand == "getheaders")
    {
        // If we are a light client we can't assist people with these requests so just return.
        //fixme:LIGHTHYBRID
        if(currentClientMode == ClientLight || ( currentClientMode == ClientHybrid && currentLoadState != LoadState_AcceptingNewBlocks ) || denyIncomingBlocks)
            return true;

        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        CBlockIndex* pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = locator.GetBlockIndex();
            if (pindex)
                pindex = pindex->pnext;
        }

        vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_PER_REQUEST;
        printf("getheaders %d to %s\n", (pindex ? pindex->nHeight : -1), hashStop.ToString().substr(0,20).c_str());
        for (; pindex; pindex = pindex->pnext)
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }


    else if (strCommand == "tx")
    {
        // Don't process incoming transactions until staking.
        if (currentClientMode != ClientFull && currentLoadState != LoadState_AcceptingNewBlocks)
            return true;

        vector<uint256> vWorkQueue;
        vector<uint256> vEraseQueue;
        CDataStream vMsg(vRecv);
        CTxDB txdb("r");
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        bool fMissingInputs = false;
        if (tx.AcceptToMemoryPool(txdb, true, &fMissingInputs))
        {
            SyncWithWallets(tx, NULL, true);
            RelayTransaction(tx, inv.hash);
            mapAlreadyAskedFor.erase(inv);
            vWorkQueue.push_back(inv.hash);
            vEraseQueue.push_back(inv.hash);

            // Recursively process any orphan transactions that depended on this one
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                uint256 hashPrev = vWorkQueue[i];
                for (set<uint256>::iterator mi = mapOrphanTransactionsByPrev[hashPrev].begin();
                     mi != mapOrphanTransactionsByPrev[hashPrev].end();
                     ++mi)
                {
                    const uint256& orphanTxHash = *mi;
                    CTransaction& orphanTx = mapOrphanTransactions[orphanTxHash];
                    bool fMissingInputs2 = false;

                    if (orphanTx.AcceptToMemoryPool(txdb, true, &fMissingInputs2))
                    {
                        printf("   accepted orphan tx %s\n", orphanTxHash.ToString().substr(0,10).c_str());
                        SyncWithWallets(tx, NULL, true);
                        RelayTransaction(orphanTx, orphanTxHash);
                        mapAlreadyAskedFor.erase(CInv(MSG_TX, orphanTxHash));
                        vWorkQueue.push_back(orphanTxHash);
                        vEraseQueue.push_back(orphanTxHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        // invalid orphan
                        vEraseQueue.push_back(orphanTxHash);
                        printf("   removed invalid orphan tx %s\n", orphanTxHash.ToString().substr(0,10).c_str());
                    }
                }
            }

            BOOST_FOREACH(uint256 hash, vEraseQueue)
                EraseOrphanTx(hash);
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx);

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nEvicted = LimitOrphanTxSize(MAX_ORPHAN_TRANSACTIONS);
            if (nEvicted > 0)
                printf("mapOrphan overflow, removed %u tx\n", nEvicted);
        }
        if (tx.nDoS)
            pfrom->Misbehaving(tx.nDoS);
    }


    else if (strCommand == "block")
    {
        if ( currentLoadState==LoadState_SyncHeadersFromEpoch || currentLoadState == LoadState_Exiting || denyIncomingBlocks )
            return true;

        LOCK(cs_Accept);

        pfrom->nLastRecvBlock = GetTime();

        CBlock block;

        // Due to the transition from PoW to PoS,
        // PoW blocks are missing the s
        try
        {
            vRecv >> block;
        }
        catch(std::ios_base::failure& e)
        {
            printf("Block didn't parse in v3 format, trying old format...\n");
            block.nVersion = 1;
            vRecv >> block;
        }

        uint256 hashBlock = block.GetHash();

        if (fDebugNet || !IsInitialBlockDownload())
        {
            printf("[%s] block %s\n", pfrom->addrName.c_str(), hashBlock.ToString().substr(0,20).c_str());
            // block.print();
        }

        CInv inv(MSG_BLOCK, hashBlock);
        pfrom->AddInventoryKnown(inv);

        if (ProcessBlock(pfrom, &block))
        {
            numSyncedBlocks++;
        }
        else
            printf("Process block failed %s\n", hashBlock.ToString().substr(0,20).c_str());

        mapAlreadyAskedFor.erase(inv);

        if (block.nDoS)
            pfrom->Misbehaving(block.nDoS);

        // Detect end of checkpoint loading.
        if (currentLoadState == LoadState_CheckPoint)
        {
            if (Checkpoints::IsCheckpoint(block.GetHash()))
            {
                // Got all of them?
                if ( Checkpoints::GetNumLoadedCheckpoints() == Checkpoints::GetNumCheckpoints() )
                {
                    // Proceed to next load state.
                    TransitionLoadState(pfrom);
                }
            }
        }
        // Detect end of all block sync.
        else if (currentLoadState == LoadState_SyncAllBlocks || currentLoadState == LoadState_SyncBlocksFromEpoch)
        {
            CheckBlockRangeEnds(pfrom);
        }
        // Carry on polling for new blocks.
        else if(currentLoadState == LoadState_AcceptingNewBlocks || currentLoadState == LoadState_VerifyAllBlocks || currentClientMode == ClientFull)
        {
            pfrom->PushGetBlocks(pindexBest, uint256(0));
        }
    }

    else if (strCommand == "headers")
    {
        if ( (currentLoadState != LoadState_SyncHeadersFromEpoch && currentLoadState != LoadState_SyncAllHeaders) || currentLoadState == LoadState_Exiting || denyIncomingBlocks)
            return true;

        vector<CBlock> vHeaders;
        vRecv >> vHeaders;

        pfrom->nLastRecvHeader = GetTime();

        CBlockIndex* pprevBest = pindexBest;
        uint256 hashLast;
        BOOST_FOREACH (CBlock& block, vHeaders)
        {
            block.vtx.clear();

            block.headerOnly = true;
            hashLast = block.GetHash();

            if(block.hashPrevBlock==pfrom->syncFrom)
                pfrom->syncFrom=block.GetHash();

            if(hashLast == pfrom->syncTo)
                break;

            if (fDebug)
            {
                printf("received header %s\n", block.GetHash().ToString().substr(0,20).c_str());
                block.print();
            }

            CInv inv(MSG_BLOCK, block.GetHash());
            pfrom->AddInventoryKnown(inv);

            // Note - checkpoints will legitimately fail here because they are already loaded.
            if (ProcessBlock(pfrom, &block))
            {
                mapAlreadyAskedFor.erase(inv);
                numSyncedHeaders++;
            }
        }
        if(fDebug)
            printf("got header(s) in range %s -> %s - %d ranges remaining\n", vHeaders[0].GetHash().ToString().c_str(), hashLast.ToString().c_str(), pfrom->NumRangesToSync);


        bool endRange=false;
        if(currentLoadState == LoadState_SyncHeadersFromEpoch || currentLoadState == LoadState_SyncAllHeaders)
        {
            if (pfrom->syncTo != uint256(0) && (pfrom->syncFrom == pfrom->syncTo || pfrom->syncFrom == mapBlockIndex[pfrom->syncTo]->pprev->GetBlockHash()))
                endRange=true;
            else if(pfrom->syncTo == uint256(0))
                if(pprevBest==pindexBest || vHeaders.size()<2000)
                    endRange=true;
        }

        if (endRange)
        {
            LOCK2(cs_vNodes, pfrom->cs_alterSyncRanges);
            pfrom->NumRangesToSync--;
            if (fDebugNetRanges)
                printf("[%s] End header range sync %s -> %s - Ranges remaining [%d]\n", pfrom->addrName.c_str(), pfrom->syncFrom.ToString().c_str(), pfrom->syncTo.ToString().c_str(), pfrom->NumRangesToSync);
            pfrom->syncFrom = uint256(0);
            pfrom->syncTo = uint256(0);
            pfrom->isSyncing = false;
            if(pfrom->NumRangesToSync == 0)
            {
                // Carry on to next load phase.
                TransitionLoadState(pfrom);
            }
            else
            {
                // Start on next range if we are waiting for any more ranges.
                // Distribute header fetching amongst as many other nodes as possible.
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    PushGetHeaderRange(pnode);
                }
            }
        }
        else
        {
            // Carry on fetching this range. (Get another 2000 headers)
            PushGetHeadersFromHash(pfrom, pfrom->syncFrom, pfrom->syncTo);
        }
    }

    else if (strCommand == "getaddr")
    {
        // Don't return addresses older than nCutOff timestamp
        int64_t nCutOff = GetTime() - (nNodeLifespan * 24 * 60 * 60);
        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        BOOST_FOREACH(const CAddress &addr, vAddr)
            if(addr.nTime > nCutOff)
                pfrom->PushAddress(addr);
    }


    else if (strCommand == "mempool")
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        for (unsigned int i = 0; i < vtxid.size(); i++) {
            CInv inv(MSG_TX, vtxid[i]);
            vInv.push_back(inv);
            if (i == (MAX_INV_SZ - 1))
                    break;
        }
        if (vInv.size() > 0)
            pfrom->PushMessage("inv", vInv);
    }


    else if (strCommand == "checkorder")
    {
        uint256 hashReply;
        vRecv >> hashReply;

        if (!GetBoolArg("-allowreceivebyip"))
        {
            pfrom->PushMessage("reply", hashReply, (int)2, string(""));
            return true;
        }

        CWalletTx order;
        vRecv >> order;

        /// we have a chance to check the order here

        // Keep giving the same key to the same ip until they use it
        if (!mapReuseKey.count(pfrom->addr))
            pwalletMain->GetKeyFromPool(mapReuseKey[pfrom->addr], true);

        // Send back approval of order and pubkey to use
        CScript scriptPubKey;
        scriptPubKey << mapReuseKey[pfrom->addr] << OP_CHECKSIG;
        pfrom->PushMessage("reply", hashReply, (int)0, scriptPubKey);
    }


    else if (strCommand == "reply")
    {
        uint256 hashReply;
        vRecv >> hashReply;

        CRequestTracker tracker;
        {
            LOCK(pfrom->cs_mapRequests);
            map<uint256, CRequestTracker>::iterator mi = pfrom->mapRequests.find(hashReply);
            if (mi != pfrom->mapRequests.end())
            {
                tracker = (*mi).second;
                pfrom->mapRequests.erase(mi);
            }
        }
        if (!tracker.IsNull())
            tracker.fn(tracker.param1, vRecv);
    }


    else if (strCommand == "ping")
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage("pong", nonce);
        }
    }


    else if (strCommand == "alert")
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert())
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, vNodes)
                        alert.RelayTo(pnode);
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                pfrom->Misbehaving(10);
            }
        }
    }


    else
    {
        // Ignore unknown commands for extensibility
    }


    // Update the last seen time for this node's address
    if (pfrom->fNetworkNode)
        if (strCommand == "version" || strCommand == "addr" || strCommand == "inv" || strCommand == "getdata" || strCommand == "ping")
            AddressCurrentlyConnected(pfrom->addr);


    return true;
}

bool ProcessMessages(CNode* pfrom)
{
    CDataStream& vRecv = pfrom->vRecv;
    if (vRecv.empty())
        return true;
    if (fDebugNet)
        printf("[%s] ProcessMessages(%lu bytes)\n", pfrom->addrName.c_str() , vRecv.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //

    // Don't let one node starve the others out - process only 20 messages per node per go.
    int count = 0;
    while (++count < 20)
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->vSend.size() >= SendBufferSize())
            break;

        // Scan for message start
        CDataStream::iterator pstart = search(vRecv.begin(), vRecv.end(), BEGIN(pchMessageStart), END(pchMessageStart));
        int nHeaderSize = vRecv.GetSerializeSize(CMessageHeader());
        if (vRecv.end() - pstart < nHeaderSize)
        {
            if ((int)vRecv.size() > nHeaderSize)
            {
                printf("\n\nPROCESSMESSAGE MESSAGESTART NOT FOUND\n\n");
                vRecv.erase(vRecv.begin(), vRecv.end() - nHeaderSize);
            }
            break;
        }
        if (pstart - vRecv.begin() > 0)
            printf("\n\nPROCESSMESSAGE SKIPPED %" PRIpdd " BYTES\n\n", pstart - vRecv.begin());
        vRecv.erase(vRecv.begin(), pstart);

        // Read header
        vector<char> vHeaderSave(vRecv.begin(), vRecv.begin() + nHeaderSize);
        CMessageHeader hdr;
        vRecv >> hdr;
        if (!hdr.IsValid())
        {
            printf("\n\nPROCESSMESSAGE: ERRORS IN HEADER %s\n\n\n", hdr.GetCommand().c_str());
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;
        if (nMessageSize > MAX_SIZE)
        {
            printf("ProcessMessages(%s, %u bytes) : nMessageSize > MAX_SIZE\n", strCommand.c_str(), nMessageSize);
            continue;
        }
        if (nMessageSize > vRecv.size())
        {
            // Rewind and wait for rest of message
            vRecv.insert(vRecv.begin(), vHeaderSave.begin(), vHeaderSave.end());
            break;
        }

        // Checksum
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = 0;
        memcpy(&nChecksum, &hash, sizeof(nChecksum));
        if (nChecksum != hdr.nChecksum)
        {
            printf("ProcessMessages(%s, %u bytes) : CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
               strCommand.c_str(), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Copy message to its own buffer
        CDataStream vMsg(vRecv.begin(), vRecv.begin() + nMessageSize, vRecv.nType, vRecv.nVersion);
        vRecv.ignore(nMessageSize);

        // Process message
        bool fRet = false;
        try
        {
            {
                LOCK(cs_main);
                fRet = ProcessMessage(pfrom, strCommand, vMsg);
            }
            if (fShutdown)
                return true;
        }
        catch (std::ios_base::failure& e)
        {
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                printf("ProcessMessages(%s, %u bytes) : Exception '%s' caught, normally caused by a message being shorter than its stated length\n", strCommand.c_str(), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                printf("ProcessMessages(%s, %u bytes) : Exception '%s' caught\n", strCommand.c_str(), nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            printf("ProcessMessage(%s, %u bytes) FAILED\n", strCommand.c_str(), nMessageSize);
    }

    vRecv.Compact();
    return true;
}


bool SendMessages(CNode* pto, bool fSendTrickle)
{
    TRY_LOCK(cs_main, lockMain);
    if (lockMain) {
        // Don't send anything until we get their version message
        if (pto->nVersion == 0)
            return true;

        // Keep-alive ping. We send a nonce of zero because we don't use it anywhere
        // right now.
        if (pto->nLastSend && GetTime() - pto->nLastSend > 30 * 60 && pto->vSend.empty()) {
            uint64_t nonce = 0;
            if (pto->nVersion > BIP0031_VERSION)
                pto->PushMessage("ping", nonce);
            else
                pto->PushMessage("ping");
        }

        //fixme: LIGHTHYBRID - do this every nth time or something possibly?
        if (currentClientMode == ClientFull || currentLoadState > LoadState_SyncAllHeaders)
        {
            // Resend wallet transactions that haven't gotten in a block yet
            ResendWalletTransactions();
        }

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    // Periodically clear setAddrKnown to allow refresh broadcasts
                    if (nLastRebroadcast)
                        pnode->setAddrKnown.clear();

                    // Rebroadcast our address
                    if (!fNoListen)
                    {
                        CAddress addr = GetLocalAddress(&pnode->addr);
                        if (addr.IsRoutable())
                            pnode->PushAddress(addr);
                    }
                }
            }
            nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            BOOST_FOREACH(const CAddress& addr, pto->vAddrToSend)
            {
                // returns true if wasn't already contained in the set
                if (pto->setAddrKnown.insert(addr).second)
                {
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage("addr", vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage("addr", vAddr);
        }


        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            BOOST_FOREACH(const CInv& inv, pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint256 hashRand = inv.hash ^ hashSalt;
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((hashRand & 3) != 0);

                    // always trickle our own transactions
                    if (!fTrickleWait)
                    {
                        CWalletTx wtx;
                        if (GetTransaction(inv.hash, wtx))
                            if (wtx.fFromMe)
                                fTrickleWait = true;
                    }

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        pto->PushMessage("inv", vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage("inv", vInv);


        //
        // Message: getdata
        //
        vector<CInv> vGetData;
        int64_t nNow = GetTime() * 1000000;
        CTxDB txdb("r");
        // Ask for at most 50 at a time don't starve other nodes.
        int count = 0;
        // We bypass the mapAskFor time when getting blocks during initial sync - always just ask for them again.
        while (!pto->mapAskFor.empty() && ( ( IsInitialBlockDownload() && ((*pto->mapAskFor.begin()).second.type == MSG_BLOCK) ) || ((*pto->mapAskFor.begin()).first <= nNow) ) && ++count < 50)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(txdb, inv))
            {
                if (fDebugNet)
                    printf("[%s] sending getdata: %s\n", pto->addrName.c_str(), inv.ToString().c_str());

                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage("getdata", vGetData);
                    vGetData.clear();
                }
                mapAlreadyAskedFor[inv] = nNow;
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage("getdata", vGetData);

    }
    return true;
}