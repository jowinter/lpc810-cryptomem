/**
 * @file
 * @brief Standalone SHA-256 hash (textbook) implementation
 */

#ifndef SHA256_H_
#define SHA256_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Length of a SHA-256 hash in bytes (32 bytes).
 */
#define SHA256_HASH_LENGTH_BYTES (32u)

/**
 * @brief Initializes a SHA-256 hash context for the hash calculation.
 */
extern void Sha256_Init(void);

/**
 * @brief Updates a SHA-256 hash context with additional hash data.
 *
 * @param[in] data points to the data buffer to be hashed.
 * @param[in] size specifies the length (in bytes) of the data buffer to be hashed.
 */
extern void Sha256_Update(const void *const data, const uint32_t size);

/**
 * @brief Finalizes a SHA-256 hash context.
 *
 * @param[out] digest is points to the location to copy the final digest to.
 *
 * @remarks This function reinitializes the hash context using a
 *   call to @ref Sha256_Init after the calculation is done.
 */
extern void Sha256_Final(uint8_t digest[SHA256_HASH_LENGTH_BYTES]);

/**
 * @brief Initializes a SHA-256 HMAC context.
 *
 * @param[in] key points to the first byte of the HMAC key.
 * @param[in] key_len is the length of the HMAC key in bytes.
 */
extern void Sha256_HmacInit(const uint8_t *const key, const uint32_t key_len);

/**
 * @brief Updates a SHA-256 HMAC context with additional hash data.
 *
 * @param[in] data points to the data buffer to be hashed.
 * @param[in] size specifies the length (in bytes) of the data buffer to be hashed.
 */
extern void Sha256_HmacUpdate(const void *const data, const uint32_t size);

/**
 * @brief Finalizes a SHA-256 HMAC context.
 *
 * @param[out] digest is points to the location to copy the final HMAC to.
 */
extern void Sha256_HmacFinal(uint8_t digest[SHA256_HASH_LENGTH_BYTES]);

#endif /* SHA256_H_ */
