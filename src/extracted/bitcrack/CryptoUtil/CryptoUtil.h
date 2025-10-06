/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CryptoUtil/CryptoUtil.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _CRYPTO_UTIL_H

namespace crypto {

	class Rng {
		unsigned int _state[16];
		unsigned int _counter;

		void reseed();

	public:
		Rng();
		void get(unsigned char *buf, int len);
	};


	void ripemd160(unsigned int *msg, unsigned int *digest);

	void sha256Init(unsigned int *digest);
	void sha256(unsigned int *msg, unsigned int *digest);

	unsigned int checksum(const unsigned int *hash);
};

#endif