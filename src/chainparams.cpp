// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>
#include <memory>

#include <chainparamsseeds.h>

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
 * testnet
CBlock(hash=000000000ba77e8a7d9d1edd0265ec446e81ac5f12e46eb42f66b05dc22e4bee, ver=0x00000003, hashPrevBlock=0000000000000000000000000000000000000000000000000000000000000000, hashMerkleRoot=30f302348e2c699306d6b1d752dde648ff8a6af35856c53e417a4c46aa81da96, nTime=1618464867, nBits=1d00ffff, nNonce=897968147, nFlags=00000000, vtx=1)
  Coinbase(hash=30f302348e, nTime=1618464668, ver=1, vin.size=1, vout.size=1, nLockTime=0)
    CTxIn(COutPoint(0000000000, 4294967295), coinbase 04ffff001d020f274530332f3230323120626c6f636b203431313339373720497420686173206265656e20372079656172732e205374696c6c206e6f74207472757374696e6720576f6c6f6e672e)
    CScriptWitness()
    CTxOut(nValue=34250071887.000000, scriptPubKey=410402f0781a2f5de02ca40615f85f)
 */
static CBlock CreateGenesisBlock(uint32_t nTimeTx, uint32_t nTimeBlock, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    // todo update block number
    const char* pszTimestamp = "03/2021 block 4113977 It has been 7 years. Still not trusting Wolong.";
    const CScript genesisOutputScript = CScript() << ParseHex("0402f0781a2f5de02ca40615f85f924a900008f57f4ae68de5080dee2f65b5393de0cd17397de2fac606a2c0269ef4266480ebaaafccaa922655f0bfc6264efe4d") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTimeTx, nTimeBlock, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.BIP16Height = 0;
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
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x8f;
        pchMessageStart[1] = 0xae;
        pchMessageStart[2] = 0xfc;
        pchMessageStart[3] = 0xeb;
        // PANDACOIN TODO: this is legacy but should still be updated
        vAlertPubKey = ParseHex("04201f0f85178950503c20d5a947883dff81e727533f1f1da104755fa25275cf68c442b8ad50da3152c10a74ea621da614d3d2048ba25a14f39bfc40c41223543a");
        nDefaultPort = 22445;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1615405884, 1615405884, 2033419875, 0x1d00ffff, 3, 34213656998.8627 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        // assert(consensus.hashGenesisBlock == uint256S("0x0000000032fe677166d54963b62a4677d8957e87c508eaa4fd7eb1c880cd27e3"));
        // assert(genesis.hashMerkleRoot == uint256S("0x3c2d8f85fab4d17aac558cc648a1a58acff0de6deb890c29985690052c5993c2"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they dont support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // vSeeds.emplace_back("seed.thepandacoin.net");

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
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
                {     0, uint256S("0x00")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block c9c065028b20a23fbb9627bbca5946c7497f11e1f72433d4d215c79047cf06f2 (height 479189).
            0, // * UNIX timestamp of last known number of transactions
            0,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0 // * estimated number of transactions per second after that timestamp
                        // 1743720/(1580808868-1345400356) = 0.007407208
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.BIP16Height = 0;
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
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000001cfc18835973");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x84c0f09e45812272c60c5704b7003abfed342964063e5b0dd51f89d8f7c3259c");

        pchMessageStart[0] = 0xdd;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0x9f;
        pchMessageStart[3] = 0xef;
        vAlertPubKey = ParseHex("04383862439513e940f6fcbf62d365c162a5256920c2c25b0b4266fdee4a443d71cfe224dbccff6fdb2ea57a37eb0cbec5637ebea06f63c70ca093672fbdc27643");
        nDefaultPort = 33445;
        nPruneAfterHeight = 1000;
        // 4169760
        genesis = CreateGenesisBlock(1618464668, 1618464867, 897968147, 0x1d00ffff, 3, 34250071887 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000000000ba77e8a7d9d1edd0265ec446e81ac5f12e46eb42f66b05dc22e4bee"));
        assert(genesis.hashMerkleRoot == uint256S("0x30f302348e2c699306d6b1d752dde648ff8a6af35856c53e417a4c46aa81da96"));

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
        fMineBlocksOnDemand = false;


        checkpointData = {
            {
                {     0, uint256S("0x000000000ba77e8a7d9d1edd0265ec446e81ac5f12e46eb42f66b05dc22e4bee")},
                {  5000, uint256S("0xfc3081849868e1b414a43e6a8138b4b848d358ed256d3a6dac6f0ec71305ac5b")}
            }
        };

        chainTxData = ChainTxData{
            // Data as of block a3fd4a0a40b96ce78700dc14745536ec31efc63eebd54a99fa49d156402db88c (height 5539)
            1620440682, // * UNIX timestamp of last known number of transactions
            10946,     // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.007324616420024983 // * estimated number of transactions per second after that timestamp
                        // 868816/(1588604853-1346029522) = 0.003581633
        };
    }
};

/**
 * Regression test
 */

class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.BIP34Height = 0; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
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
        vAlertPubKey = ParseHex("04383862439513e940f6fcbf62d365c162a5256920c2c25b0b4266fdee4a443d71cfe224dbccff6fdb2ea57a37eb0cbec5637ebea06f63c70ca093672fbdc27643");
        nDefaultPort = 44445;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1615405884, 1615405884, 2033419875, 0x1d00ffff, 3, 34213656998.8627 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000000b10442d84a88b1ce00dd43fe06de95cccd2ab6dc8dba6c0a2ef8354a"));
        assert(genesis.hashMerkleRoot == uint256S("0x793456b78b27dcff4915c5acb005001c15365e831407cb43bf9db02405839ca3"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        checkpointData = {
            {
                {0, uint256S("0x00000000b10442d84a88b1ce00dd43fe06de95cccd2ab6dc8dba6c0a2ef8354a")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,55);  // pandacoin: addresses begin with 'P'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,22); // pandacoin: addresses begin with 'p'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,183);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "pcrt";

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}
