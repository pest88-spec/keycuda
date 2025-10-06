/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CryptoUtil/Rng.cpp
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#include <stdio.h>
#include <string.h>
#include <string>
#include "CryptoUtil.h"

#ifdef _WIN32
#include<Windows.h>
#include <bcrypt.h>

static void secureRandom(unsigned char *buf, unsigned int count)
{
	BCRYPT_ALG_HANDLE h;
	BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, NULL, 0);
	BCryptGenRandom(h, buf, count, 0);
}
#else
static void secureRandom(unsigned char *buf, unsigned int count)
{
	// Read from /dev/urandom
	FILE *fp = fopen("/dev/urandom", "rb");

	if(fp == NULL) {
		throw std::string("Fatal error: Cannot open /dev/urandom for reading");
	}

	if(fread(buf, 1, count, fp) != count) {
		throw std::string("Fatal error: Not enough entropy available in /dev/urandom");
	}

	fclose(fp);
}
#endif


crypto::Rng::Rng()
{
	reseed();
}

void crypto::Rng::reseed()
{
	_counter = 0;

	memset(_state, 0, sizeof(_state));

	secureRandom((unsigned char *)_state, 32);
}

void crypto::Rng::get(unsigned char *buf, int len)
{
	int i = 0;
	while(len > 0) {
		if(_counter++ == 0xffffffff) {
			reseed();
		}

		_state[15] = _counter;

		unsigned int digest[8];
		sha256Init(digest);
		sha256(_state, digest);

		if(len >= 32) {
			memcpy(&buf[i], (const void *)digest, 32);
			i += 32;
			len -= 32;
		} else {
			memcpy(&buf[i], (const void *)digest, len);
			i += len;
			len -= len;
		}
	}
}