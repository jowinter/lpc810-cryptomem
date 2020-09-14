/**
 * @file
 * @brief Standalone SHA-256 + HMAC (textbook) software implementation (little-endian)
 */
#include "Sha256.h"

#include <Hal.h>


/**
 * @brief SHA-256 (and HMAC) calculation context
 */
typedef struct
{
	/**
	 * @brief Hash state variables
	 */
	uint32_t H[8u];

	/**
	 * @brief Message word buffer (for circular word schedule)
	 */
	uint32_t W[16u];

	/**
	 * @brief Total length of the message in byte
	 */
	uint32_t msg_length;

	/**
	 * @brief Holding area for ipad/opad (in HMAC calculations)
	 */
	uint8_t pad[SHA256_HASH_LENGTH_BYTES];
} Sha256_Ctx_t;

/**
 * @brief Global (singleton) instance of SHA
 */
static Sha256_Ctx_t gSha256;

/**
 * @brief SHA-256 Initial Hash Values
 */
static const uint32_t gkSha256_IV[8u] =
{
		UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
		UINT32_C(0x510e527f), UINT32_C(0x9b05688c), UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
};

/**
 * @brief SHA-256 Round Constants
 */
static const uint32_t gkSha256_K[64u] =
{
	   UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
	   UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
	   UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
	   UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
	   UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
	   UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
	   UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
	   UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
	   UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
	   UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
	   UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
	   UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
	   UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
	   UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
	   UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
	   UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

//---------------------------------------------------------------------------------------------------------------------
static inline uint32_t ROR(const uint32_t value, const uint32_t pos)
{
	return (value >> pos) | (value << (32u - pos));
}

//---------------------------------------------------------------------------------------------------------------------
static inline uint32_t SHR(const uint32_t value, const uint32_t pos)
{
	return (value >> pos);
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief Schedules the next message word of a given SHA context.
 *
 * @param[in] ctx is the SHA-256 hash context to be used.
 * @param[in] i is the current round number (in range 0..63)
 */
static uint32_t Sha256_ScheduleNextWord(Sha256_Ctx_t *const ctx, const uint32_t i)
{
	// The SHA-256 message word schedule  (for rounds >= 16) is typically written as:
	//   s0 := ROR(w[i-15], 7)  ^ ROR(w[i-15], 18) ^ SHR(w[i-15], 3);
	//   s1 := ROR(w[i- 2], 17) ^ ROR(w[i- 2], 19) ^ SHR(w[i- 2], 10);
	//   w[i] := w[i-16] + s0 + w[i-7] + s1
	//
	// An identical version of this word schedule can be written using W[0..15] as circular
	// buffer (The same basic idea is explained in detail in the SHA-1 specification and can
	// be adapted easily for SHA-256 on memory constrained devices).
	//
	uint32_t W_i;

	if (i < 16u)
	{
		// Rounds 0 to 15. Directly load the word from the buffer (taking byte order adjustments into account)
		W_i = __REV(ctx->W[i]);
	}
	else
	{
		// Rounds 16 to 63. Full word schedule is active
		const uint32_t W_m15 = ctx->W[(i +  1u) % 16u];
		const uint32_t s0    = ROR(W_m15, 7u) ^ ROR(W_m15, 18u) ^ SHR(W_m15, 3u);

		const uint32_t W_m2  = ctx->W[(i + 14u) % 16u];
		const uint32_t s1    = ROR(W_m2, 17u) ^ ROR(W_m2,  19u) ^ SHR(W_m2, 10u);

		// Schedule the current word
		W_i  = ctx->W[i % 16u] + s0 + ctx->W[(i + 9u) % 16u] + s1;
	}

	// Update the circular buffer
	ctx->W[i % 16u]      = W_i;

	return W_i;
}

//---------------------------------------------------------------------------------------------------------------------
/**
 * @brief Processes the current message block of the given SHA context.
 */
static void Sha256_Process(Sha256_Ctx_t *const ctx)
{
	// Load the working variables from the current hash state
	uint32_t a = ctx->H[0u];
	uint32_t b = ctx->H[1u];
	uint32_t c = ctx->H[2u];
	uint32_t d = ctx->H[3u];
	uint32_t e = ctx->H[4u];
	uint32_t f = ctx->H[5u];
	uint32_t g = ctx->H[6u];
	uint32_t h = ctx->H[7u];

	// Iterate the round functions
	for (uint32_t i = 0u; i <= 63u; ++i)
	{
		// Step 1: Schedule the next message word
		const uint32_t W_i   = Sha256_ScheduleNextWord(ctx, i);

		// Step 2: Evaluate the round function
		const uint32_t S0    = ROR(a, 2u) ^ ROR(a, 13u) ^ ROR(a, 22u);
		const uint32_t S1    = ROR(e, 6u) ^ ROR(e, 11u) ^ ROR(e, 25u);
		const uint32_t ch    = ( e & f) ^ (~e & g);
		const uint32_t maj   = (a & b) ^ ( a & c) ^ (b & c);
		const uint32_t tmp_1 = h + S1 + ch + gkSha256_K[i] + W_i;
		const uint32_t tmp_2 = S0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + tmp_1;
		d = c;
		c = b;
		b = a;
		a = tmp_1 + tmp_2;
	}

	// Update the hash state
	ctx->H[0u] += a;
	ctx->H[1u] += b;
	ctx->H[2u] += c;
	ctx->H[3u] += d;
	ctx->H[4u] += e;
	ctx->H[5u] += f;
	ctx->H[6u] += g;
	ctx->H[7u] += h;

	// Clear the word buffer
	//
	// This serves a twofold purpose: Firstly we reduce the risk of information leakage, and secondly we simplify
	// handling of the final padding (as unused parts of the buffer are always zeroed out).
	//
	__builtin_memset(&ctx->W[0u], 0u, sizeof(ctx->W));
}

//---------------------------------------------------------------------------------------------------------------------
__USED void Sha256_Init(void)
{
	Sha256_Ctx_t *const ctx = &gSha256;

	// Setup the initial hash value
	__builtin_memcpy(&ctx->H[0u], &gkSha256_IV[0u], SHA256_HASH_LENGTH_BYTES);

	// Clear the word buffer
	__builtin_memset(&ctx->W[0u], 0u, sizeof(ctx->W));

	// Assume initial message length 0
	ctx->msg_length = 0u;
}

//---------------------------------------------------------------------------------------------------------------------
__USED void Sha256_Update(const void *const data, const uint32_t size)
{
	Sha256_Ctx_t *const ctx = &gSha256;

	const uint8_t *src = (const uint8_t *) data;
	uint32_t remaining = size;

	while (remaining > 0u)
	{
		// Determine the remaining capacity of the word buffer
		//
		// Note that we maintain the invariant buf_capacity > 0 in the entire loop,
		// since buf_offset is always _strictly_ less than sizeof(ctx->W).
		//
		const uint32_t buf_offset      = ctx->msg_length % sizeof(ctx->W);
		const uint32_t buf_capacity    = sizeof(ctx->W) - buf_offset;
		const uint32_t size_to_process = (remaining < buf_capacity) ? remaining : buf_capacity;

		// Fill the word buffer
		uint8_t *const buf_data = (uint8_t *) &ctx->W[0u];
		__builtin_memcpy(buf_data + buf_offset, src, size_to_process);

		// Flush the block if needed
		if (sizeof(ctx->W) == (buf_offset + size_to_process))
		{
			Sha256_Process(ctx);
		}

		// Advance the source pointer and the remaining size
		remaining       -= size_to_process;
		src             += size_to_process;
		ctx->msg_length += size_to_process;
	}
}

//---------------------------------------------------------------------------------------------------------------------
__USED void Sha256_Final(uint8_t digest[SHA256_HASH_LENGTH_BYTES])
{
	Sha256_Ctx_t *const ctx = &gSha256;

	// Append the 0x80 padding byte
	const uint8_t padding_byte = UINT8_C(0x80);
	Sha256_Update(&padding_byte, 1);

	// We explicitly need to flush the buffer if the remaining buffer capacity is less than 8 bytes
	// (which we need for the final bit counter).
	if ((ctx->msg_length % sizeof(ctx->W)) > (sizeof(ctx->W) - 8u))
	{
		Sha256_Process(ctx);
	}

	// Append the bit (sic!) size of the message and process the final block.
	//
	// Note that our BUG5_Sha256_Process() ensures that the W buffer is cleared after processing a block, which
	// as a side effect guarantees proper zero padding for us.
	//
	// FIXME: This implementation currently does not provide any special handling for larger buffers that exceed
	// the 32-bit limit for bit-sizes.
	//
	// NOTE: At this point ctx->msg_length is equal to the actual message length plus 1 extra byte (for the 0x80 padding)
	ctx->W[15u] = __REV((ctx->msg_length - 1u) << 3u);

	// Process the final block
	Sha256_Process(ctx);

	// Copy out the final hash
	for (uint32_t i = 0u; i < 8u; ++i)
	{
		__UNALIGNED_UINT32_WRITE(&digest[i * 4u], __REV(ctx->H[i]));
	}

	// Re-initialize the hash context
	Sha256_Init();
}

//---------------------------------------------------------------------------------------------------------------------
__USED void Sha256_HmacInit(const uint8_t *const key, const uint32_t key_len)
{
	Sha256_Ctx_t *const ctx = &gSha256;

	// Initialize the hash context
	Sha256_Init();

	if (key_len > SHA256_HASH_LENGTH_BYTES)
	{
		// Key length is greater than block size (need to hash once)
		Sha256_Update(key, key_len);
		Sha256_Final(&ctx->pad[0u]);
	}
	else
	{
		// Copy the key (and pad with zeros)
		__builtin_memcpy(&ctx->pad[0u], &key[0u], key_len);
		__builtin_memset(&ctx->pad[key_len], 0u, SHA256_HASH_LENGTH_BYTES - key_len);
	}

	// Prepare the ipad value
	for (size_t i = 0u; i < SHA256_HASH_LENGTH_BYTES; ++i)
	{
		ctx->pad[i] ^= 0x36u;
	}

	// Start the inner hash (with ipad)
	Sha256_Update(&ctx->pad[0u], SHA256_HASH_LENGTH_BYTES);

	// Prepare the opad value
	for (size_t i = 0u; i < SHA256_HASH_LENGTH_BYTES; ++i)
	{
		ctx->pad[i] ^= (0x36u ^ 0x5Cu);
	}
}

//---------------------------------------------------------------------------------------------------------------------
__USED void Sha256_HmacUpdate(const void *const data, const uint32_t size)
{
	Sha256_Update(data, size);
}

//---------------------------------------------------------------------------------------------------------------------
__USED void Sha256_HmacFinal(uint8_t digest[SHA256_HASH_LENGTH_BYTES])
{
	Sha256_Ctx_t *const ctx = &gSha256;

	// Finalize the inner hash
	Sha256_Final(digest);

	// Now compute the outer hash
	Sha256_Update(&ctx->pad[0u], SHA256_HASH_LENGTH_BYTES);
	Sha256_Update(digest, SHA256_HASH_LENGTH_BYTES);
	Sha256_Final(digest);

	// And done
	__builtin_memset(&ctx->pad[0u], 0u, SHA256_HASH_LENGTH_BYTES);
}
