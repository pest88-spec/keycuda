/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/AddressUtil/AddressUtil.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _ADDRESS_UTIL_H
#define _ADDRESS_UTIL_H

#include "secp256k1.h"

namespace Address {
	std::string fromPublicKey(const secp256k1::ecpoint &p, bool compressed = false);
	bool verifyAddress(std::string address);
};

namespace Base58 {
	std::string toBase58(const secp256k1::uint256 &x);
	secp256k1::uint256 toBigInt(const std::string &s);
	void getMinMaxFromPrefix(const std::string &prefix, secp256k1::uint256 &minValueOut, secp256k1::uint256 &maxValueOut);

	void toHash160(const std::string &s, unsigned int hash[5]);

	bool isBase58(std::string s);
};



namespace Hash {


	void hashPublicKey(const secp256k1::ecpoint &p, unsigned int *digest);
	void hashPublicKeyCompressed(const secp256k1::ecpoint &p, unsigned int *digest);

	void hashPublicKey(const unsigned int *x, const unsigned int *y, unsigned int *digest);
	void hashPublicKeyCompressed(const unsigned int *x, const unsigned int *y, unsigned int *digest);

};


#endif