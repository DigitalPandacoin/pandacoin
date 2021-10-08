// Copyright (c) 2012-2021 The Peercoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef PANDACOIN_KERNEL_H
#define PANDACOIN_KERNEL_H

#include <primitives/transaction.h> // CTransaction(Ref)

class CBlockIndex;
class BlockValidationState;
class CBlockHeader;
class CBlock;


// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

const int REQUIRED_POS_DEPTH = 4;

// Protocol switch time of v0.3 kernel protocol
extern unsigned int nProtocolV03SwitchTime;
extern unsigned int nProtocolV03TestSwitchTime;

// Whether a given coinstake is subject to new v0.3 protocol
bool IsProtocolV03(unsigned int nTimeCoinStake);
// Whether a given block is subject to new v0.4 protocol
bool IsProtocolV04(unsigned int nTimeBlock);
// Whether a given transaction is subject to new v0.5 protocol
bool IsProtocolV05(unsigned int nTimeTx);
// Whether a given block is subject to new v0.6 protocol
// Test against previous block index! (always available)
bool IsProtocolV06(const CBlockIndex *pindexPrev);
// Whether a given transaction is subject to new v0.7 protocol
bool IsProtocolV07(unsigned int nTimeTx);
// Whether a given block is subject to new BIPs from bitcoin 0.16.x
bool IsBTC16BIPsEnabled(uint32_t nTimeTx);
// Whether a given timestamp is subject to new v0.9 protocol
bool IsProtocolV09(unsigned int nTimeTx);
// pandacoin: Whether a miner is able to earn rewards
bool IsRewardsEnabled(unsigned int nTime);
// pandacoin: Require PoS block to happen before new PoW blocks
bool IsPoWPoSDependence(unsigned int nTime);

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(const CBlockIndex* pindexCurrent, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
bool CheckStakeKernelHash(unsigned int nBits, CBlockIndex* pindexPrev, const CBlockHeader& blockFrom, unsigned int nTxPrevOffset, const CTransactionRef& txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, bool fPrintProofOfStake=false);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(BlockValidationState &state, CBlockIndex* pindexPrev, const CTransactionRef &tx, unsigned int nBits, uint256& hashProofOfStake);

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum);

bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned int nRequired, unsigned int nToCheck);

// peercoin: entropy bit for stake modifier if chosen by modifier
unsigned int GetStakeEntropyBit(const CBlock& block);

bool IsPoWAccepted(const CBlockIndex* pindex, int depth = REQUIRED_POS_DEPTH);

#endif // PANDACOIN_KERNEL_H
