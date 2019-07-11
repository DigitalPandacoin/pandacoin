// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/foreach.hpp>

#include "checkpoints.h"

#include "txdb.h"
#include "main.h"
#include "uint256.h"


static const int nCheckpointSpan = 10;

namespace Checkpoints
{
    typedef std::map<int, uint256> MapCheckpoints;

    //
    // What makes a good checkpoint block?
    // + Is surrounded by blocks with reasonable timestamps
    //   (no blocks before with a timestamp after, none after with
    //    timestamp before)
    // + Contains no strange transactions
    //

    // Pandacoin
    // Checkpoints evenly and frequently dispersed as hybrid/light mode use them to fetch from multiple peers.
    // i.e. We fetch one range (gap between two checkpoints) per peer simeltaneously.
    // Note that these are queued from back to front i.e. newest blocks are fetched first.
    // We delibritely use very small gaps for the first six or so checkpoints to avoid a situation where the last 'range' to be synced goes to a slow node while all others sit idle.
    static MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        ( 0      , hashGenesisBlock )
        ( 2000   , uint256("0xfbe01f6aafd2744636c9fbd93914f3748c9c9f8a02b728c31b7872729a918e36"))
        ( 4000   , uint256("0x011f4d8c682d8e733041598a2d2fe4e912aa7341e7eb1ed15f3a0a19f0ed3dc9"))
        ( 8000   , uint256("0x69490b5c69632ce6873104fbb629b6ce424686fde794a8545761be96c549e52a"))
        ( 12000  , uint256("0x9b6bca69d584e03bd33667084f3c9dd14041feda021f6a5a191588797e8fb887"))
        ( 16000  , uint256("0x9fd1c8e0edf22d227a91762e2b280a22015aa0c6b297fbae4065e844265f2e29"))
        ( 20000  , uint256("0xc38a1c0f26528b59e515fb7b231ee34f9bbc9320ca02eef098fb67f6697a66f9"))
        ( 25000  , uint256("0xa32dd6449e324b8fa976f2b5cbc9982fdccfd70d72c13029f0f1056d136ba0ed"))
        ( 30000  , uint256("0x28bcb59e7fc81bf47757ca582e1e40c39bfb03cbe927118eb96f081634e7546e"))
        ( 35000  , uint256("0x533425cb2b0782fed9dee9b9afcf0e058b5d0349df8f41094dbdd4cd8cb195f4"))
        ( 40000  , uint256("0x9aa904e4b2e60a8e8831f81eb5bb1573bde1e0bfb68c1ab7e1d19fada98a9125"))
        ( 50000  , uint256("0x57ce013451cc3d863961e1e92d1f8b0bfc239b58d0f6bb4a7be4aff6fee839e3"))
        ( 75000  , uint256("0x1b13fa665f6939e0f44adb25b5731102fb2f6623bfb3f5c03185b36d20982a7c"))
        ( 100000 , uint256("0x5a9d2be35519126617341153f915ebca6cb0a1652b1cc17b8eac8789846fed1e"))
        ( 125000 , uint256("0x4221da6c38fc9e677ffb811ae81f1162cb792e2f6bc5baa04b77f6cc9130606b"))
        ( 150000 , uint256("0xc2cf4286f1a826d790ac58d8fa81f894dc3c46ef219db6e44347e611725bb269"))
        ( 175000 , uint256("0x5f6e3878ce9b04a3dcb2a1e8cd69ac766c0c98f8da3afa363d4451ba787891b5"))
        ( 200000 , uint256("0x4c2a371e1711c8514551a78af9a2b1747e24bc92982d0bb1065accd04bfe134e"))
        ( 225000 , uint256("0x9c879a56429cc8052176f299d4de4ab43b9a3df0129c1ca465b934e31ff51eb5"))
        ( 250000 , uint256("0x1c3a4b56916aeaa7d5ea24cf8d2ffaff610ccf07e8a3bf1fd662b431b8b854b9"))
        ( 275000 , uint256("0x643199f1a124662c344cadbc5cd67614b7155e4024fa7070ec705df52fedc2b7"))
        ( 300000 , uint256("0x2dda276404bd738a95c05961e77721a5b79896c614375361bd7d6857577f4b82"))
        ( 325000 , uint256("0xf6bfec312bf55226574eafc329c18056ac8c94277d7e33ca06592fbb4fd71f27"))
        ( 350000 , uint256("0xcb43cae668d5efe204daa278cf313fb5cb09d57089939aa845fc1085ba1be89b"))
        ( 375000 , uint256("0xf70679d589e481e6d4c05bbfb2ae67c604135e63fa80973c1f5671769cb4a4e4"))
        ( 400000 , uint256("0x27fbc9e87af89407c4419ceabcd1a7bfae2f90cf7b0521401171ab420a48a61b"))
        ( 425000 , uint256("0x47a375b1b561fae4b6f10cd79707683af7b17dc2baebd5aa4291206696fcb81a"))
        ( 450000 , uint256("0xc8906cafc1f2d09963869120bd239fb5f14a701c54e61db7ed6685d13572ab4c"))
        ( 475000 , uint256("0x65bface2efe48220f196b85c59a2b191f3700ae0c6c329df42c3f10c26bab440"))
        ( 500000 , uint256("0x1338f27a71738f392fe01035757d31278abae752e6c63ca6235e5e5bd78677f8"))
		( 525000 , uint256("0xb5152602b468d0a621bd70d35e399ad3fe5d70387e838aadfecea23846926a0d"))
		( 550000 , uint256("0xdf2ecf6247e78f96642c869695f2cd72cdc6588b1ce686c457fbf67d3d867845"))
		( 575000 , uint256("0xe9e069013179d91cb4da50c0869e2707c6f7c712be7ecf267ee3ca7673f027a9"))
		( 600000 , uint256("0xb68d3207335511ffbe682a2b15c600c94cec0d1f8c7a5507b784c4c603204560"))
		( 625000 , uint256("0xd6ee8fb407fc96f04c4ee813ac832a89149985796952f3e064fea28292c80a4a"))
		( 650000 , uint256("0x858adabec398087e64b8e73e18bc4ad8bcd72947f2ddb354ee225957db1eb4f3"))
		( 675000 , uint256("0x49f45d02f1feda8a9e23a6a6c7574ba73a600951e2f54a114844acc71e71101f"))
		( 700000 , uint256("0x5811996950f0ae6a4f1950db9f9673e523d76120b9ff2bc97596e47940c078db"))
		( 725000 , uint256("0x63038048a1c6a0ef85182da01dc96e5e86fc7243a2490c95f8f7fc94a20b9d2b"))
		( 750000 , uint256("0xd4f336ab69c7dab3e5be0177c4f86ca3d0d74acddf6b484ad2d0a4822346fe1b"))
		( 775000 , uint256("0xbfb086b734db59e9f4fdf1b1c064f01893b81ded7f57f643b0ed89e7582345a6"))
		( 800000 , uint256("0xbec46b2df4bd69fa41e995665d8c1d198206a635ce74ef72c3d79479fff0294e"))
		( 825000 , uint256("0x023cccc09e98e52bf690aef7da91870fd8c9d773b056654b923d966afa06a1b8"))
		( 850000 , uint256("0xd1b51ec1a978c2abb8666abb8b065bc211686b03ed62df50a643d716c26afce2"))
		( 875000 , uint256("0xfba7db7de0b4e826e28f6da7569e3f5b8d4e3df1f1b0c14b1b94664358c2534d"))
		( 900000 , uint256("0xcfa0210062013d53da50c2155675b0beeee5d163f3146535e640acf7bd3a9ce9"))
		( 925000 , uint256("0xdbcb37045dc615c5889c387dbdd3154bbfb19c6ba6f3493735e79f483822e9e2"))
		( 950000 , uint256("0xb0286b673f59a6d7a2002ff8d818ba620763bca9d953f6c83602ace0e2851666"))
		( 975000 , uint256("0xa3da2386047d89a182c856a37990b193b59567125c2d52034f7e9857db45da86"))
		( 1000000 , uint256("0x55756e522e0cdc08f92fe94176adc3858f76d9af7775a8f9e4c6441bdf7fc549"))
		( 1025000 , uint256("0xf859239e412921efe7c9f89e2c488cd8366715ab64752cb9335665a878f1b6a6"))
		( 1050000 , uint256("0xf97e24bd157b82541910f696fb8846e627b482b4f923cbb60c51c2b42e032cd2"))
		( 1075000 , uint256("0xcd5fd91ec659285ceb5fe3b8979ced02f36e784901945f6b4baee4400489d35a"))
		( 1100000 , uint256("0xc21f82674ab75b8e32fb8154f358cda196f34b67ac5ff893e1a049732dc92bd4"))
		( 1125000 , uint256("0x79f30a9f563a3c2f36364129913e3d40c17edbc17797a2014fc11c4fdf0d10d2"))
		( 1150000 , uint256("0x1df9d8e9ee20becf69067c8bbcb7891844826715864118b5e82754dea1499ba1"))
		( 1175000 , uint256("0x35de50e67ee9bb223ff09368d46f3e32572f79974b586a1014250908e00fab63"))
		( 1200000 , uint256("0x5966553028101ce6572c1bdc22515c36d9b90138584b204867d0119228ca33c6"))
		( 1225000 , uint256("0x91348e03cf4eb657ee9a327a255f92d6eaeb74cf907b22e9fadc0bed3e6842e7"))
		( 1250000 , uint256("0xc677da1a7ea67d5f349f6ac4eb318009e58008039171f10b40abfae2b25f6a1d"))
		( 1275000 , uint256("0x3128d86d2acb414e0f5965cf66754242a88ba47df62ed16eda7b2bdaf1a4f4fc"))
		( 1300000 , uint256("0x1ec82e2d359d62c532145b8381ba7b108c797ba8838ffb03874a8a007abd15c6"))
		( 1325000 , uint256("0x4e544a2a068530bc62b8ff559fe09820fceab62548a839fda65a87f8bf7d77b6"))
		( 1350000 , uint256("0x79b8f02f465f16d878128eed44b5be6a2a93a1c229eba32b12b58a240cd721e9"))
		( 1375000 , uint256("0x93114c7eb1660ca5dcc7771f45dccca1b8b5a9739a9ed3a5073fa9f1fe204fd9"))
		( 1400000 , uint256("0xe1aed6166a0a403c6ace59b0489254ac389f6e10a972d1c07782bc28adcb43e2"))
		( 1425000 , uint256("0xca1561233327deb2b2186359cc96044f95629e9159bd0722d309398c2322a9b6"))
		( 1450000 , uint256("0x18e37d5c2b7ad7c387564e7b86702c052ac846f275a92e1b7e890c958ae758c3"))
		( 1475000 , uint256("0xd51893d581bb76d750608ceecba3633d271940612c081a02b82e3c7b94b1c1fa"))
		( 1500000 , uint256("0xd2cf90abb9d3cabe294f10de0e853a194d2f7743ffc5c1106ad871345ec38fd0"))
		( 1525000 , uint256("0x28cbd12be61bfd06747990e3b2aaaaee45a1ef6b161540a6f00b6efb2426892f"))
		( 1550000 , uint256("0x498d859b6f48bd7d4f0a6166db6e47ebcc5edcf9da0d8a2243a5804493e31c2e"))
		( 1575000 , uint256("0xcbb7fa93553edae5445fd4961657f514b630dccdb7e21b35b860fbea4978f6b4"))
		( 1600000 , uint256("0xd2c8e32bdf04e0e7a734c4645e6e39d442968210b071f7318e91d425890df522"))
		( 1625000 , uint256("0x03e74edec3e9b1d81d95ff5e1e13c6c1c399e7248ada7de51581c9160d6fed6d"))
		( 1650000 , uint256("0xca676b1ef1df9a063e50bb41864530ca5f6668f86990bb6d6d2100287fdc016d"))
		( 1675000 , uint256("0x9c1f93be50cce8c6ff28a4dca3e607befe29d037de1d60af68c0a2044078f9c2"))
		( 1700000 , uint256("0x1514fe377cf76afb85f3ffee15e678c5fbc5da6ece9824d781a28fbaf0976ba4"))
		( 1725000 , uint256("0xb7e7bd5813514ae6c991561ff009e1ee6c26bd91ac11d44756e8257a64619def"))
		( 1750000 , uint256("0x2f2fdca6df5d8a815da1e9df73276302251d5b4c0b6641d9bfbb3c00af5e212b"))
		( 1775000 , uint256("0x38a00dc82dae2e63b095b1c63a8346ff4f28db0b0a775cc657a243abc19bc5ee"))
		( 1800000 , uint256("0xbc5aaffd25495de3bfa5faabf5df569fc904bc97f63d145766560625a19c732c"))
		( 1825000 , uint256("0x7a3ab3143777f69500d4c8cece2cd5d65820e0309c594aefc2d3ad24bf91ef97"))
		( 1850000 , uint256("0xea7ad3a092ebf141b7d182242eedf474315300f22bf68105d1e56b21c7f20339"))
        ( 1875000 , uint256("0xa01e1dfa04f1c555a5ef00795819a8db72ec0a2375ccdc46c8d72d16747b9965"))
		( 1900000 , uint256("0xfda16636ad72d18344d320bd371e6aff3d04717492ca2f82251a801e8a89c44f"))
        ( 1925000 , uint256("0xe515e281c85f0d754ac83435431e4c14c8c20cc6727c6a8be0eb7c99fb740e66"))
        ( 1950000 , uint256("0x447a4b20d0191e736dbc4ddeac37a0f1be1851965417c1a8093ebe93b176fc2a"))
        ( 1975000 , uint256("0xe84313720860894a0caeddae70ea8fbee2ecb34194b79950d738ef787e4ed1bc"))
        ( 2000000 , uint256("0x77832670490d870ffe67ddf49d35972ad4cd7df1be31bd61bc7d3b80d65c4ecd"))
		( 2025000 , uint256("0x85814c29dbcd59013cbc2b8969febe6b921c215904a8d3d1aac7854aa3204347"))
		( 2050000 , uint256("0x4913453a5ac589918afd2e0523bd0cc469a9b00d6f4d7b22785e1047f08e68b6"))
		( 2075000 , uint256("0x26fd63f32597f6b30191a6c7151e0c8ebda2a93dd9b8cc94c176e648d67cb91b"))
		( 2100000 , uint256("0x97e0cacad500672ce438191998b3fe7f44d528d55c67364bcd0d23efc09b8d11"))
		( 2125000 , uint256("0x3933e38aeb91b084d6059b277223a6b8601d9b686ed932dabcc423623313173a"))
		( 2150000 , uint256("0x6cca8bf2afd148aa251753e1ec45dd6f6f536dadf622cae9b1f9e513290c5ad4"))
		( 2175000 , uint256("0x6bba3a2f1fda8168c1fc7c0384a4bbeee01b122aae5cbf811fcd57c8ecabdcb6"))
		( 2200000 , uint256("0x966f9aabeee43f34f9af6f2db7d6dd9024174a05a035823d482139bf7ac5993a"))
		( 2225000 , uint256("0xbdc7d6bc94877c911230c0f1e26d9375484e0801834e109d9826d1bbd9580d83"))
		( 2250000 , uint256("0x081b47473f1fc74a1fdc86eae6fd41be28a9a270f2829958fb1092b14a75e005"))
		( 2275000 , uint256("0xb769f8b6a3eb20675d8422795c0b904cc36a1a8ac15dab939339b5b38b5eeffa"))
		( 2300000 , uint256("0x8e43b62a6575253a794ca572045de789cfe8f3cc30eeb170c178539ee76efa74"))
		( 2325000 , uint256("0x48bcd7830db40499ef03a49966f64a4452e1b45aeeb5acc3d50cf5c8499a71b4"))
		( 2350000 , uint256("0xc8ba576f8ee1afdd20df3c3c3d8bfd725de77642d7cb3c8dc4fd15156bbd56fb"))
		( 2375000 , uint256("0x5b2c137b1bab5279862d957ab03c92c57579250b04161dd17b7693e4d5e2804c"))
		( 2400000 , uint256("0xe7a4791c4b8efea26325f160a835bad1df277ad0fa305750ae4916bfd9a9f843"))
		( 2425000 , uint256("0x1c52202f75522d2814ec56f99291ca4a2b2a865130e1d882448defd80c37a8ac"))
		( 2450000 , uint256("0xdad9a7e912869688ce310b358f1e6019e39b86132ce8dbded4b9ec100e27ae11"))
		( 2475000 , uint256("0x54dc7712a0cfc73f395c9f73aa6e4c47919a5ffe919c5f958a6e739336efbe5a"))
	    ( 2500000 , uint256("0x6d8ac1cd599d83e6fead03a7c11fde0f2725130d96b359b1b50fde95b2d97fb8"))
	    ( 2525000 , uint256("0xade768c7bec28cc72036e25245eb3647a344de8615620225f0313f30d6db24bc"))
	    ( 2550000 , uint256("0xb01af6310e793d43a9f40570e869603cb39032ff790219a8bde2e7de38c9dbd7"))
	    ( 2575000 , uint256("0xd632503f5460e9652a2a1e03e390bed9ea8f01d4d5090a62f8b1f73036787afb"))
	    ( 2600000 , uint256("0x3b63276e34bce2ba41fd8ce707aa01d2bae40799a663ac84ac5da0bf25f1846c"))
	    ( 2625000 , uint256("0x2a28734db0d20037a16b51cd2e1e54d64056fc2db339f611f5f16007309a1594"))
		( 2650000 , uint256("0xb8cba46ac7f722bc9969795cfbcc0bfc72019af89bfca51f103be61642c4bad4"))
		( 2675000 , uint256("0xec3fc51067f49cc5d554027fbcea9b265ac44bea612f717686405a11aa8fee34"))
		( 2700000 , uint256("0xec8883a85434fdf220779e2748469b7447812951e59d8e82080e663218c5c8d3"))
		( 2725000 , uint256("0x428c8e59b3246b70b18dedadff22e2b5f172bdfbe33cd4b491b89fa5ad9cd7f8"))
		( 2750000 , uint256("0x8f3a4cb792b345f7ce88933f19e03ac35ec9a92e4586e3320e724ea218a81f68"))
		( 2775000 , uint256("0xef0283abeb2a380c3d600fe1acf63d384480699e896666b018c36289ec6ceb40"))
		( 2800000 , uint256("0x6c7c9493c5c62768271d4cc431b22f8d7a96b0349c8d087f050465b4d48b8db6"))
		( 2825000 , uint256("0x4cbecee25b53ba1619667cf56ab23dadf09fe03bd87b9ea313efc762f11f109d"))
        ( 2850000 , uint256("0x76234ad8661974e4261af2f58908ba0092b63f6440b7cb83b983e39ce7999763"))
        ( 2875000 , uint256("0x92c927a231a68c353abced8b7aad93f4a4c37d7b9ff1c5887dbe1630d4046f8c"))
        ( 2900000 , uint256("0xa037069c52ad341026c69d44017ca6323aba91a3d964b05e1443459a61e7b47a"))
        ( 2925000 , uint256("0x56bbea2a53919b0a4b5703b0514df6ad739fdd04dc0171f411741e5dc33c49ba"))
        ( 2950000 , uint256("0x88b2113825b190726981af829ed4ddf77d360c18c3618a282b978db2a926418b"))
        ( 2970000 , uint256("0x2571ccf4421a2b2e52c1a47511d82c6f6e71465d8df893f1592b9fde8510987d"))
        ( 3000000 , uint256("0xf6a4a02ab3eabadbae312a670abccf9477a79a6d90fdd257a49279ae982ff611"))
        ( 3025000 , uint256("0x2cee6349b48867fe78bf59193746d875875224657ee1171eeab921fdcbe033c1"))
        ( 3050000 , uint256("0x5ba6f6031ad18dc9459e3479ec120a2d76d1a18285d28d143943eeca2064b9be"))
        ( 3075000 , uint256("0x2fcbe342cea9c37d3a822107e91f3ee5e1feb72ebc60635636de8c8e48289a3b"))
        ( 3100000 , uint256("0x307832bc8138de9a26373c4063011d78e7aa27ab7f48ca79a5bcb7edf02c36b1"))
;

    // TestNet has no checkpoints
    static MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 0, hashGenesisBlockTestNet )
        ;

    bool CheckHardened(int nHeight, const uint256& hash)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
        if (i == checkpoints.end()) return true;
        return hash == i->second;
    }

    int GetTotalBlocksEstimate()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        return checkpoints.rbegin()->first;
    }

    bool IsCheckpoint(uint256 hash)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            if(i.second == hash)
            {
                return true;
            }
        }
        return false;
    }

    void LoadCheckpoints(CNode* pfrom)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            if(i.second != hashGenesisBlock)
            {
                if( !mapOrphanBlocks.count(i.second) && !mapBlockIndex.count(i.second) )
                {
                    pfrom->AskFor(CInv(MSG_BLOCK, i.second));
                }
            }
        }
    }

    int GetNumCheckpoints()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        return checkpoints.size();
    }

    int GetNumLoadedCheckpoints()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        int Ret=0;
        MapCheckpoints::const_iterator i = checkpoints.begin();
        for(;i != checkpoints.end();i++)
        {
            if( mapOrphanBlocks.count(i->second) || mapBlockIndex.count(i->second) )
            {
                Ret++;
            }
        }
        return Ret;
    }

    void InsertPlaceHoldersBetweenCheckpoints()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        MapCheckpoints::const_iterator i = checkpoints.begin();
        int64_t prevIndex=0;
        uint256 prevHash;
        while(i != checkpoints.end())
        {
            if (mapOrphanBlocks.count(i->second))
            {
                CBlock placeHolderBlock;
                placeHolderBlock.hashOverride = mapOrphanBlocks[i->second]->hashPrevBlock;
                placeHolderBlock.hashPrevBlock = prevHash;
                placeHolderBlock.numPlacesHeld = i->first - prevIndex;
                placeHolderBlock.headerOnly = true;
                placeHolderBlock.placeHolderBlock = true;
                ProcessBlock(NULL, &placeHolderBlock);
            }
            prevHash = i->second;
            prevIndex = i->first;
            // Check if already in tree - when loading an existing block index this may be the case.
            if (mapBlockIndex.count(prevHash))
            {
                prevHash = pindexBest->GetBlockHash();
                prevIndex = pindexBest->nHeight;
            }

            i++;
        }
    }

    CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            const uint256& hash = i.second;
            std::map<uint256, CBlockIndex*>::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return NULL;
    }

    uint256 GetEpochHash(int64_t datetime)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            if(mapBlockIndex.count(i.second) && mapBlockIndex[i.second]->nTime < datetime)
            {
                CBlockIndex* pEpochBlock = mapBlockIndex[i.second];
                if (epochCheckpointDepth == 0)
                {
                    while(pEpochBlock->pnext && !pEpochBlock->pnext->IsHeaderOnly())
                        pEpochBlock = pEpochBlock->pnext;
                    epochCheckpointDepth = pEpochBlock->nHeight;
                }
                return i.second;
            }
        }
        // This should never be reached as we should always have checkpoints.
        return uint256(0);
    }

    // ppcoin: synchronized checkpoint (centrally broadcasted)
    uint256 hashSyncCheckpoint = 0;
    uint256 hashPendingCheckpoint = 0;
    CSyncCheckpoint checkpointMessage;
    CSyncCheckpoint checkpointMessagePending;
    uint256 hashInvalidCheckpoint = 0;
    CCriticalSection cs_hashSyncCheckpoint;

    // ppcoin: get last synchronized checkpoint
    CBlockIndex* GetLastSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        if (!mapBlockIndex.count(hashSyncCheckpoint))
            error("GetSyncCheckpoint: block index missing for current sync-checkpoint %s", hashSyncCheckpoint.ToString().c_str());
        else
            return mapBlockIndex[hashSyncCheckpoint];
        return NULL;
    }

    // ppcoin: only descendant of current sync-checkpoint is allowed
    bool ValidateSyncCheckpoint(uint256 hashCheckpoint)
    {
        if (!mapBlockIndex.count(hashSyncCheckpoint))
            return error("ValidateSyncCheckpoint: block index missing for current sync-checkpoint %s", hashSyncCheckpoint.ToString().c_str());
        if (!mapBlockIndex.count(hashCheckpoint))
            return error("ValidateSyncCheckpoint: block index missing for received sync-checkpoint %s", hashCheckpoint.ToString().c_str());

        CBlockIndex* pindexSyncCheckpoint = mapBlockIndex[hashSyncCheckpoint];
        CBlockIndex* pindexCheckpointRecv = mapBlockIndex[hashCheckpoint];

        if (pindexCheckpointRecv->nHeight <= pindexSyncCheckpoint->nHeight)
        {
            // Received an older checkpoint, trace back from current checkpoint
            // to the same height of the received checkpoint to verify
            // that current checkpoint should be a descendant block
            CBlockIndex* pindex = pindexSyncCheckpoint;
            while (pindex->nHeight > pindexCheckpointRecv->nHeight)
                if (!(pindex = pindex->pprev))
                    return error("ValidateSyncCheckpoint: pprev null - block index structure failure");
            if (pindex->GetBlockHash() != hashCheckpoint)
            {
                hashInvalidCheckpoint = hashCheckpoint;
                return error("ValidateSyncCheckpoint: new sync-checkpoint %s is conflicting with current sync-checkpoint %s", hashCheckpoint.ToString().c_str(), hashSyncCheckpoint.ToString().c_str());
            }
            return false; // ignore older checkpoint
        }

        // Received checkpoint should be a descendant block of the current
        // checkpoint. Trace back to the same height of current checkpoint
        // to verify.
        CBlockIndex* pindex = pindexCheckpointRecv;
        while (pindex->nHeight > pindexSyncCheckpoint->nHeight)
            if (!(pindex = pindex->pprev))
                return error("ValidateSyncCheckpoint: pprev2 null - block index structure failure");
        if (pindex->GetBlockHash() != hashSyncCheckpoint)
        {
            hashInvalidCheckpoint = hashCheckpoint;
            return error("ValidateSyncCheckpoint: new sync-checkpoint %s is not a descendant of current sync-checkpoint %s", hashCheckpoint.ToString().c_str(), hashSyncCheckpoint.ToString().c_str());
        }
        return true;
    }

    bool WriteSyncCheckpoint(const uint256& hashCheckpoint)
    {
        CTxDB txdb;
        txdb.TxnBegin();
        if (!txdb.WriteSyncCheckpoint(hashCheckpoint))
        {
            txdb.TxnAbort();
            return error("WriteSyncCheckpoint(): failed to write to db sync checkpoint %s", hashCheckpoint.ToString().c_str());
        }
        if (!txdb.TxnCommit())
            return error("WriteSyncCheckpoint(): failed to commit to db sync checkpoint %s", hashCheckpoint.ToString().c_str());

        Checkpoints::hashSyncCheckpoint = hashCheckpoint;
        return true;
    }

    bool AcceptPendingSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        if (hashPendingCheckpoint != 0 && mapBlockIndex.count(hashPendingCheckpoint))
        {
            if (!mapBlockIndex.count(hashPendingCheckpoint) || !ValidateSyncCheckpoint(hashPendingCheckpoint))
            {
                hashPendingCheckpoint = 0;
                checkpointMessagePending.SetNull();
                return false;
            }

            CTxDB txdb;
            CBlockIndex* pindexCheckpoint = mapBlockIndex[hashPendingCheckpoint];
            if (!pindexCheckpoint->IsInMainChain())
            {
                CBlock block;
                if (!block.ReadFromDisk(pindexCheckpoint))
                    return error("AcceptPendingSyncCheckpoint: ReadFromDisk failed for sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
                if (!block.SetBestChain(txdb, pindexCheckpoint))
                {
                    hashInvalidCheckpoint = hashPendingCheckpoint;
                    return error("AcceptPendingSyncCheckpoint: SetBestChain failed for sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
                }
            }

            if (!WriteSyncCheckpoint(hashPendingCheckpoint))
                return error("AcceptPendingSyncCheckpoint(): failed to write sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
            hashPendingCheckpoint = 0;
            checkpointMessage = checkpointMessagePending;
            checkpointMessagePending.SetNull();
            printf("AcceptPendingSyncCheckpoint : sync-checkpoint at %s\n", hashSyncCheckpoint.ToString().c_str());
            // relay the checkpoint
            if (!checkpointMessage.IsNull())
            {
                BOOST_FOREACH(CNode* pnode, vNodes)
                    checkpointMessage.RelayTo(pnode);
            }
            return true;
        }
        return false;
    }

    // Automatically select a suitable sync-checkpoint
    uint256 AutoSelectSyncCheckpoint()
    {
        const CBlockIndex *pindex = pindexBest;
        // Search backward for a block within max span and maturity window
        while (pindex->pprev && (pindex->GetBlockTime() + nCheckpointSpan * GetTargetSpacing(pindex->nHeight) > pindexBest->GetBlockTime() || pindex->nHeight + nCheckpointSpan > pindexBest->nHeight))
            pindex = pindex->pprev;
        return pindex->GetBlockHash();
    }

    // Check against synchronized checkpoint
    bool CheckSync(const uint256& hashBlock, const CBlockIndex* pindexPrev)
    {
        if (fTestNet) return true; // Testnet has no checkpoints
        int nHeight = pindexPrev->nHeight + 1;

        LOCK(cs_hashSyncCheckpoint);
        // sync-checkpoint should always be accepted block
        assert(mapBlockIndex.count(hashSyncCheckpoint));
        const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];

        if (nHeight > pindexSync->nHeight)
        {
            // trace back to same height as sync-checkpoint
            const CBlockIndex* pindex = pindexPrev;
            while (pindex->nHeight > pindexSync->nHeight)
                if (!(pindex = pindex->pprev))
                    return error("CheckSync: pprev null - block index structure failure");
            if (pindex->nHeight < pindexSync->nHeight || pindex->GetBlockHash() != hashSyncCheckpoint)
                return false; // only descendant of sync-checkpoint can pass check
        }
        if (nHeight == pindexSync->nHeight && hashBlock != hashSyncCheckpoint)
            return false; // same height with sync-checkpoint
        if (nHeight < pindexSync->nHeight && !mapBlockIndex.count(hashBlock))
            return false; // lower height than sync-checkpoint
        return true;
    }

    bool WantedByPendingSyncCheckpoint(uint256 hashBlock)
    {
        LOCK(cs_hashSyncCheckpoint);
        if (hashPendingCheckpoint == 0)
            return false;
        if (hashBlock == hashPendingCheckpoint)
            return true;
        if (mapOrphanBlocks.count(hashPendingCheckpoint)
            && hashBlock == WantedByOrphan(mapOrphanBlocks[hashPendingCheckpoint]))
            return true;
        return false;
    }

    // ppcoin: reset synchronized checkpoint to last hardened checkpoint
    bool ResetSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        const uint256& hash = mapCheckpoints.rbegin()->second;
        if (mapBlockIndex.count(hash) && !mapBlockIndex[hash]->IsInMainChain())
        {
            // checkpoint block accepted but not yet in main chain
            printf("ResetSyncCheckpoint: SetBestChain to hardened checkpoint %s\n", hash.ToString().c_str());
            CTxDB txdb;
            CBlock block;
            if (!block.ReadFromDisk(mapBlockIndex[hash]))
                return error("ResetSyncCheckpoint: ReadFromDisk failed for hardened checkpoint %s", hash.ToString().c_str());
            if (!block.SetBestChain(txdb, mapBlockIndex[hash]))
            {
                return error("ResetSyncCheckpoint: SetBestChain failed for hardened checkpoint %s", hash.ToString().c_str());
            }
        }
        else if(!mapBlockIndex.count(hash))
        {
            // checkpoint block not yet accepted
            hashPendingCheckpoint = hash;
            checkpointMessagePending.SetNull();
            printf("ResetSyncCheckpoint: pending for sync-checkpoint %s\n", hashPendingCheckpoint.ToString().c_str());
        }

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, mapCheckpoints)
        {
            const uint256& hash = i.second;
            if (mapBlockIndex.count(hash) && mapBlockIndex[hash]->IsInMainChain())
            {
                if (!WriteSyncCheckpoint(hash))
                    return error("ResetSyncCheckpoint: failed to write sync checkpoint %s", hash.ToString().c_str());
                printf("ResetSyncCheckpoint: sync-checkpoint reset to %s\n", hashSyncCheckpoint.ToString().c_str());
                return true;
            }
        }

        return false;
    }

    void AskForPendingSyncCheckpoint(CNode* pfrom)
    {
        LOCK(cs_hashSyncCheckpoint);
        if (pfrom && hashPendingCheckpoint != 0 && (!mapBlockIndex.count(hashPendingCheckpoint)) && (!mapOrphanBlocks.count(hashPendingCheckpoint)))
            pfrom->AskFor(CInv(MSG_BLOCK, hashPendingCheckpoint));
    }

    bool SetCheckpointPrivKey(std::string strPrivKey)
    {
        // Test signing a sync-checkpoint with genesis block
        CSyncCheckpoint checkpoint;
        checkpoint.hashCheckpoint = !fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet;
        CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
        sMsg << (CUnsignedSyncCheckpoint)checkpoint;
        checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

        std::vector<unsigned char> vchPrivKey = ParseHex(strPrivKey);
        CKey key;
        key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end())); // if key is not correct openssl may crash
        if (!key.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
            return false;

        // Test signing successful, proceed
        CSyncCheckpoint::strMasterPrivKey = strPrivKey;
        return true;
    }

    bool SendSyncCheckpoint(uint256 hashCheckpoint)
    {
        CSyncCheckpoint checkpoint;
        checkpoint.hashCheckpoint = hashCheckpoint;
        CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
        sMsg << (CUnsignedSyncCheckpoint)checkpoint;
        checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

        if (CSyncCheckpoint::strMasterPrivKey.empty())
            return error("SendSyncCheckpoint: Checkpoint master key unavailable.");
        std::vector<unsigned char> vchPrivKey = ParseHex(CSyncCheckpoint::strMasterPrivKey);
        CKey key;
        key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end())); // if key is not correct openssl may crash
        if (!key.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
            return error("SendSyncCheckpoint: Unable to sign checkpoint, check private key?");

        if(!checkpoint.ProcessSyncCheckpoint(NULL))
        {
            printf("WARNING: SendSyncCheckpoint: Failed to process checkpoint.\n");
            return false;
        }

        // Relay checkpoint
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
                checkpoint.RelayTo(pnode);
        }
        return true;
    }

    // Is the sync-checkpoint outside maturity window?
    bool IsMatureSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        // sync-checkpoint should always be accepted block
        assert(mapBlockIndex.count(hashSyncCheckpoint));
        const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];
        return (nBestHeight >= pindexSync->nHeight + nCoinbaseMaturity ||
                pindexSync->GetBlockTime() + nStakeMinAge < GetAdjustedTime());
    }
}

// ppcoin: sync-checkpoint master key
const std::string CSyncCheckpoint::strMasterPubKey = "04ef014b36647e8433a2cedf76f1d6ea0bc5914ba936fadceda90d7472da3cf442469d3a1ab5ee416e7428726761dd3188bda3d0ae163db491f8ca0bdad92a0506";

std::string CSyncCheckpoint::strMasterPrivKey = "";

// ppcoin: verify signature of sync-checkpoint message
bool CSyncCheckpoint::CheckSignature()
{
    CKey key;
    if (!key.SetPubKey(ParseHex(CSyncCheckpoint::strMasterPubKey)))
        return error("CSyncCheckpoint::CheckSignature() : SetPubKey failed");
    if (!key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
        return error("CSyncCheckpoint::CheckSignature() : verify signature failed");

    // Now unserialize the data
    CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
    sMsg >> *(CUnsignedSyncCheckpoint*)this;
    return true;
}

// ppcoin: process synchronized checkpoint
bool CSyncCheckpoint::ProcessSyncCheckpoint(CNode* pfrom)
{
    if (!CheckSignature())
        return false;

    LOCK(Checkpoints::cs_hashSyncCheckpoint);
    if (!mapBlockIndex.count(hashCheckpoint))
    {
        // We haven't received the checkpoint chain, keep the checkpoint as pending
        Checkpoints::hashPendingCheckpoint = hashCheckpoint;
        Checkpoints::checkpointMessagePending = *this;
        printf("ProcessSyncCheckpoint: pending for sync-checkpoint %s\n", hashCheckpoint.ToString().c_str());
        if(currentClientMode == ClientFull || currentLoadState == LoadState_AcceptingNewBlocks)
        {
            // Ask this guy to fill in what we're missing
            if (pfrom)
            {
                pfrom->PushGetBlocks(pindexBest, hashCheckpoint);
                // ask directly as well in case rejected earlier by duplicate
                // proof-of-stake because getblocks may not get it this time
                pfrom->AskFor(CInv(MSG_BLOCK, mapOrphanBlocks.count(hashCheckpoint)? WantedByOrphan(mapOrphanBlocks[hashCheckpoint]) : hashCheckpoint));
            }
        }
        return false;
    }

    if (!Checkpoints::ValidateSyncCheckpoint(hashCheckpoint))
        return false;

    CTxDB txdb;
    CBlockIndex* pindexCheckpoint = mapBlockIndex[hashCheckpoint];
    if (!pindexCheckpoint->IsInMainChain())
    {
        // checkpoint chain received but not yet main chain
        CBlock block;
        if (!block.ReadFromDisk(pindexCheckpoint))
            return error("ProcessSyncCheckpoint: ReadFromDisk failed for sync checkpoint %s", hashCheckpoint.ToString().c_str());
        if (!block.SetBestChain(txdb, pindexCheckpoint))
        {
            Checkpoints::hashInvalidCheckpoint = hashCheckpoint;
            return error("ProcessSyncCheckpoint: SetBestChain failed for sync checkpoint %s", hashCheckpoint.ToString().c_str());
        }
    }

    if (!Checkpoints::WriteSyncCheckpoint(hashCheckpoint))
        return error("ProcessSyncCheckpoint(): failed to write sync checkpoint %s", hashCheckpoint.ToString().c_str());
    Checkpoints::checkpointMessage = *this;
    Checkpoints::hashPendingCheckpoint = 0;
    Checkpoints::checkpointMessagePending.SetNull();
    printf("ProcessSyncCheckpoint: sync-checkpoint at %s\n", hashCheckpoint.ToString().c_str());
    return true;
}
