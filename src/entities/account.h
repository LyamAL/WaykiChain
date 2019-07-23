// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef ENTITIES_ACCOUNT_H
#define ENTITIES_ACCOUNT_H

#include <boost/variant.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset.h"
#include "crypto/hash.h"
#include "id.h"
#include "stake.h"
#include "vote.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

using namespace json_spirit;

class CAccountDBCache;
class CAccountToken;

typedef map<TokenSymbol, CAccountToken> AccountTokenMap;
typedef CoinType AssetType;

string GetAccountTokenStr(AccountTokenMap &tokens) {
    string str;
    for (auto iter : tokens) {
        string symbol = iter.first;
        uint64_t free_value = iter.second.free_value;
        uint64_t staked_value = iter.second.staked_value;
        uint64_t frozen_value = iter.second.frozen_value;
        str += strprintf ("\n  - %s: {free=%lld, staked=%lld, frozen=%lld}\n",
                    symbol, free_value, staked_value, frozen_value);
    }
    return str;
}
enum BalanceType : uint8_t {
    NULL_TYPE   = 0,
    FREE_VALUE,
    STAKED_VALUE,
    FROZEN_VALUE
};

enum BalanceOpType : uint8_t {
    NULL_OP     = 0,    //!< invalid op
    ADD_FREE    = 1,    //!< external send coins to this account
    SUB_FREE    = 2,    //!< send coins to external account
    STAKE,              //!< free   -> staked
    UNSTAKE,            //!< staked -> free
    FREEZE,             //!< free   -> frozen
    UNFREEZE            //!< frozen -> free
};

class CAccountToken {
public:
    uint64_t free_tokens;
    uint64_t frozen_tokens; //held by open DEX orders
    uint64_t staked_tokens; //for staking purposes

public:
    CAccountToken(uint64_t &freeTokens, uint64_t &frozenTokens) :
                    free_tokens(freeTokens), frozen_tokens(frozenTokens), staked_tokens(stakedTokens) { }

    CAccountToken& operator=(const CAccountToken& other) {
        if (this == &other) return *this;

        this->free_tokens       = other.free_tokens;
        this->frozen_tokens     = other.frozen_tokens;
        this->staked_tokens     = other.staked_tokens;

        return *this;
    }

    IMPLEMENT_SERIALIZE(
        READWRITE(VARINT(free_tokens));
        READWRITE(VARINT(frozen_tokens));
        READWRITE(VARINT(staked_tokens));)
};

/**
 * Common or Contract Account
 */
class CAccount {
public:
    CKeyID  keyid;                  //!< unique: keyId of the account (interchangeable to address) - 20 bytes
    CRegID  regid;                  //!< unique: regId - derived from 1st TxCord - 6 bytes
    CNickID nickid;                 //!< unique: Nickname ID of the account (sting maxlen=32) - 8 bytes

    CPubKey owner_pubkey;           //!< account public key
    CPubKey miner_pubkey;           //!< miner saving account public key

    AccountTokenMap tokens;         //!< In total, 3 types of coins/tokens:
                                    //!<    1) system-issued coins: WICC, WGRT
                                    //!<    2) miner-issued stablecoins WUSD|WCNY|...
                                    //!<    3) user-issued tokens (WRC20 compilant)

    uint64_t received_votes;        //!< votes received
    uint64_t last_vote_height;      //!< account's last vote block height used for computing interest

    mutable uint256 sigHash;        //!< in-memory only

public:
    CAccount() : CAccount(CKeyID(), CNickID(), CPubKey()) {}
    CAccount(const CAccount& other) { *this = other; }
    CAccount& operator=(const CAccount& other) {
        if (this == &other) return *this;

        this->keyid             = other.keyid;
        this->regid             = other.regid;
        this->nickid            = other.nickid;
        this->owner_pubkey      = other.owner_pubkey;
        this->miner_pubkey      = other.miner_pubkey;
        this->received_votes    = other.received_votes;
        this->last_vote_height  = other.last_vote_height;
        this->tokens            = other.tokens;

        return *this;
    }
    CAccount(const CKeyID& keyIdIn): keyid(keyIdIn), regid(), nickid(), received_votes(0), last_vote_height(0) {}
    CAccount(const CKeyID& keyidIn, const CNickID& nickidIn, const CPubKey& ownerPubkeyIn)
        : keyid(keyidIn), nickid(nickidIn), owner_pubkey(ownerPubkeyIn), received_votes(0), last_vote_height(0) {
        miner_pubkey = CPubKey();
        tokens.Clear();
        regid.Clear();
    }

    std::shared_ptr<CAccount> GetNewInstance() const { return std::make_shared<CAccount>(*this); }

public:

    IMPLEMENT_SERIALIZE(
        READWRITE(keyid);
        READWRITE(regid);
        READWRITE(nickid);
        READWRITE(owner_pubkey);
        READWRITE(miner_pubkey);
        READWRITE(tokens);
        READWRITE(VARINT(received_votes));
        READWRITE(VARINT(last_vote_height));)

    uint256 GetHash(bool recalculate = false) const {
        if (recalculate || sigHash.IsNull()) {
            CHashWriter ss(SER_GETHASH, 0);
            ss  << keyid << regid << nickid << owner_pubkey << miner_pubkey
                << tokens << VARINT(received_votes) << VARINT(last_vote_height);

            sigHash = ss.GetHash();
        }

        return sigHash;
    }

    bool GetBalance(const TokenSymbol &tokenSymbol, const BalanceType balanceType, uint64_t &value);
    bool OperateBalance(const TokenSymbol &tokenSymbol, const BalanceOpType opType, const uint64_t &value);

    bool UndoOperateAccount(const CAccount& accountInfo);

    bool ProcessDelegateVotes(const vector<CCandidateVote>& candidateVotesIn,
                              vector<CCandidateVote>& candidateVotesInOut,
                              const uint64_t currHeight,
                              const CAccountDBCache* pAccountCache);

    bool HaveOwnerPubKey() const { return owner_pubkey.IsFullyValid(); }
    bool RegIDIsMature() const;

    uint64_t GetTotalBcoins(const vector<CCandidateVote>& candidateVotes, const uint64_t currHeight);
    uint64_t GetVotedBCoins(const vector<CCandidateVote>& candidateVotes, const uint64_t currHeight);

    uint64_t ComputeVoteStakingInterest(const vector<CCandidateVote> &candidateVotes, const uint64_t currHeight);
    uint64_t ComputeBlockInflateInterest(const uint64_t currHeight) const;

    bool IsEmptyValue() const { return !(free_bcoins > 0); }
    bool IsEmpty() const { return keyid.IsEmpty(); }
    void SetEmpty() { keyid.SetEmpty(); }  // TODO: need set other fields to empty()??
    string ToString() const;
    Object ToJsonObj() const;

    CAccountToken GetToken(const TokenSymbol &tokenSymbol);
    bool SetToken(const TokenSymbol &tokenSymbol, const CAccountToken &accountToken);

private:
    bool IsBcoinWithinRange(uint64_t nAddMoney);
    bool IsFcoinWithinRange(uint64_t nAddMoney);

};

enum ACCOUNT_TYPE {
    // account type
    REGID      = 0x01,  //!< Registration account id
    BASE58ADDR = 0x02,  //!< Public key
};

/**
 * account operate produced in contract
 * TODO: rename CVmOperate
 */
class CVmOperate{
public:
	unsigned char accountType;      //!< regid or base58addr
	unsigned char accountId[34];	//!< accountId: address
	unsigned char opType;		    //!< OperType
	unsigned int  timeoutHeight;    //!< the transacion Timeout height
	unsigned char money[8];			//!< The transfer amount

	IMPLEMENT_SERIALIZE
	(
		READWRITE(accountType);
		for (int i = 0; i < 34; i++)
			READWRITE(accountId[i]);
		READWRITE(opType);
		READWRITE(timeoutHeight);
		for (int i = 0; i < 8; i++)
			READWRITE(money[i]);
	)

	CVmOperate() {
		accountType = REGID;
		memset(accountId, 0, 34);
		opType = NULL_OP;
		timeoutHeight = 0;
		memset(money, 0, 8);
	}

	Object ToJson();
};


#endif //ENTITIES_ACCOUNT_H