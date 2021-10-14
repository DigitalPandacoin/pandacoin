// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <versionbitsinfo.h>

#include <assert.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTimeTx, uint32_t nTimeBlock, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(9999) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;
    txNew.nTime = nTimeTx;

    CBlock genesis;
    genesis.nTime    = nTimeBlock;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTimeTx, uint32_t nTimeBlock, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "05/2021 block 4217950 It has been 7 years. Still not trusting Wolong.";
    const CScript genesisOutputScript = CScript() << ParseHex("0402f0781a2f5de02ca40615f85f924a900008f57f4ae68de5080dee2f65b5393de0cd17397de2fac606a2c0269ef4266480ebaaafccaa922655f0bfc6264efe4d") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTimeTx, nTimeBlock, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        //consensus.BIP16Height = 0;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.powLimit =            uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 32;
        consensus.bnInitialHashTarget = uint256S("0000000000ffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 40;

        consensus.nTargetTimespan = 7 * 24 * 60 * 60;  // one week
        consensus.nStakeTargetSpacing = 10 * 60; // 10-minute block spacing
        consensus.nTargetSpacingWorkMax = 12 * consensus.nStakeTargetSpacing; // 2-hour
        consensus.nPowTargetSpacing = consensus.nStakeTargetSpacing;
        consensus.nStakeMinAge = 60 * 60 * 24 * 30; // minimum age for coin age
        consensus.nStakeMaxAge = 60 * 60 * 24 * 90;
        consensus.nModifierInterval = 6 * 60 * 60; // Modifier interval: time to elapse before new modifier is computed
        consensus.nCoinbaseMaturity = 500;

        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000078bafa76d43af");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xa83074a354d968b09df059aeb1daf75ef1e5b70f55fd7f19c2717874f0949ca5");

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xb5;
        pchMessageStart[1] = 0xca;
        pchMessageStart[2] = 0x9d;
        pchMessageStart[3] = 0xba;
        vAlertPubKey = ParseHex("041ad89ff3b34612a56b04a843538f7ce6ac01f1b514fcea924b71b082187fb2a4953f0278b28bae5ab0c6fa3451e239bb303ba0140e53d73a97bbc65a55696719");
        nDefaultPort = 22445;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 0;
        // block 4217950
        genesis = CreateGenesisBlock(1621011262, 1621021021, 389690407, 0x1d00ffff, 3, (34342707157 + 20000000) * COIN); // + 20 million delta for staking rewards during transition on old main net
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000000003fa90365cae3b93740a5a08810a42dd122f87383afd45655a0101224"));
        assert(genesis.hashMerkleRoot == uint256S("0x404275f90d16c9a886a86be0e701cb2e87c7e25a4e92697bd9a4df30f92e4efa"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("seed.thepandacoin.net");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,55);  // pandacoin: addresses begin with 'P'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,22); // pandacoin: addresses begin with 'p'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,183);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // human readable prefix to bench32 address
        bech32_hrp = "pn";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {    0, uint256S("0x000000003fa90365cae3b93740a5a08810a42dd122f87383afd45655a0101224")},
                { 2215, uint256S("0x1e596dbb7417c84e75428781eab0035ef30cefaea48079eaccd6974e949eb5d2")},
                { 7000, uint256S("0x9fb62c4eb3e0a61eb368a5edd3e22634dbc5ad6cd11cac0a8fed034b083ba37f")},
                {18000, uint256S("0xa83074a354d968b09df059aeb1daf75ef1e5b70f55fd7f19c2717874f0949ca5")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block ff65454ebdf1d89174bec10a3c016db92f7b1d9a4759603472842f254be8d7b3 (height 1636).
            1634237485, // * UNIX timestamp of last known number of transactions
            36281,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the ChainStateFlushed debug.log lines)
            0.004944084615700324  // * estimated number of transactions per second after that timestamp
                        // 1797921/(1591618067-1345400356) = 0.00730216
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        //consensus.BIP16Height = 0;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.powLimit =            uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 28;
        consensus.bnInitialHashTarget = uint256S("00000007ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 29;

        consensus.nTargetTimespan = 7 * 24 * 60 * 60;  // one week
        consensus.nStakeTargetSpacing = 10 * 60;  // 10-minute block spacing
        consensus.nTargetSpacingWorkMax = 12 * consensus.nStakeTargetSpacing; // 2-hour
        consensus.nPowTargetSpacing = consensus.nStakeTargetSpacing;
        consensus.nStakeMinAge = 60 * 60 * 24; // test net min age is 1 day
        consensus.nStakeMaxAge = 60 * 60 * 24 * 90;
        consensus.nModifierInterval = 60 * 20; // Modifier interval: time to elapse before new modifier is computed
        consensus.nCoinbaseMaturity = 60;

        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000003126f8109c1c0d7");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x16ea0c9099e14a4eda8d37e10c75e9a16023fe88d15e2510d7acd8702ac393d0");

        pchMessageStart[0] = 0xad;
        pchMessageStart[1] = 0xaf;
        pchMessageStart[2] = 0xc9;
        pchMessageStart[3] = 0xd6;
        vAlertPubKey = ParseHex("0401b81a47ef1c6999530f3242c13520a214c100b7a0f4b67292a78f87feda84522d429d686e9a8f9a544a6f93e083ff823ead75b2f67f1c3e6a92c4f1176d0c80");
        nDefaultPort = 33445;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 2;
        m_assumed_chain_state_size = 0;

        genesis = CreateGenesisBlock(1621011262, 1621021021, 389690407, 0x1d00ffff, 3, (34342707157 + 20000000) * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000000003fa90365cae3b93740a5a08810a42dd122f87383afd45655a0101224"));
        assert(genesis.hashMerkleRoot == uint256S("0x404275f90d16c9a886a86be0e701cb2e87c7e25a4e92697bd9a4df30f92e4efa"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("tseed.thepandacoin.net");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // human readable prefix to bench32 address
        bech32_hrp = "tpn";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {    0, uint256S("0x000000003fa90365cae3b93740a5a08810a42dd122f87383afd45655a0101224")},
                {10800, uint256S("0x16ea0c9099e14a4eda8d37e10c75e9a16023fe88d15e2510d7acd8702ac393d0")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block 000000003f7d1f4b55d529fdcf3983c3c8a969ab0900dee41bed0bdaf1c3a9d7 (height 444932)
            1634237042, // * UNIX timestamp of last known number of transactions
            20918,     // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.006780192544661512 // * estimated number of transactions per second after that timestamp
                        // 868816/(1588604853-1346029522) = 0.003581633
        };
    }
};

/**
 * Regression test
 */

class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 0; // BIP34 activated on regtest (Used in functional tests) || pandacoin used to have 0 here
        consensus.BIP34Hash = uint256();
        consensus.powLimit =            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 28;
        consensus.bnInitialHashTarget = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~arith_uint256(0) >> 29;

        consensus.nTargetTimespan = 7 * 24 * 60 * 60; // two weeks
        consensus.nStakeTargetSpacing = 10 * 60; // 10-minute block spacing
        consensus.nTargetSpacingWorkMax = 12 * consensus.nStakeTargetSpacing; // 2-hour
        consensus.nPowTargetSpacing = consensus.nStakeTargetSpacing;

        consensus.nStakeMinAge = 60 * 60 * 24; // test net min age is 1 day
        consensus.nStakeMaxAge = 60 * 60 * 24 * 90;
        consensus.nModifierInterval = 60 * 20; // Modifier interval: time to elapse before new modifier is computed
        consensus.nCoinbaseMaturity = 60;

        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xdd;
        pchMessageStart[1] = 0x99;
        pchMessageStart[2] = 0xa0;
        pchMessageStart[3] = 0xfe;
        vAlertPubKey = ParseHex("04d5fa086d27af54e936a108e5b945af1a3cc3cdf281daa91cd7cabc213feb9e5f88de9c7ce50fc4b26e90f71b2da2ad4f25fed934ab027b3d9653ebf8bf929271");
        nDefaultPort = 44445;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        genesis = CreateGenesisBlock(1621011262, 1621021021, 389690407, 0x1d00ffff, 3, (34342707157 + 20000000) * COIN);

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000000003fa90365cae3b93740a5a08810a42dd122f87383afd45655a0101224"));
        assert(genesis.hashMerkleRoot == uint256S("0x404275f90d16c9a886a86be0e701cb2e87c7e25a4e92697bd9a4df30f92e4efa"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        m_is_test_chain = true;
        m_is_mockable_chain = true;
        checkpointData = {
            {
                {0, uint256S("0x000000003fa90365cae3b93740a5a08810a42dd122f87383afd45655a0101224")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "pnrt";
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        //fMineBlocksOnDemand = true;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (gArgs.IsArgSet("-segwitheight")) {
        int64_t height = gArgs.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
    }
 }

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams(gArgs));
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}
