/* Copyright (c) IAIK, Graz University of Technology, 2015.
 * All rights reserved.
 * Contact: http://opensource.iaik.tugraz.at
 * 
 * This file is part of the Secure Block Device Library.
 * 
 * Commercial License Usage
 * Licensees holding valid commercial licenses may use this file in
 * accordance with the commercial license agreement provided with the
 * Software or, alternatively, in accordance with the terms contained in
 * a written agreement between you and SIC. For further information
 * contact us at http://opensource.iaik.tugraz.at.
 * 
 * Alternatively, this file may be used under the terms of the GNU General
 * Public License as published by the Free Software Foundation version 2.
 * 
 * The Secure Block Device Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with the Secure Block Device Library. If not, see <http://www.gnu.org/licenses/>.
 */
///
/// \file
/// \brief Implements the Secure Block Device Library's user interface (its
/// API).
///
/// This implementation uses the SBD block layer to implement the SBD's API to
/// the user (you).
///

#include "sbdi_siv.h"
#include "sbdi_nocrypto.h"
#include "sbdi_ocb.h"
#include "sbdi_hmac.h"

#include "SecureBlockDeviceInterface.h"

#include <string.h>

static inline void sbdi_init(sbdi_t *sbdi, sbdi_pio_t *pio, mt_t *mt,
    sbdi_bc_t *cache)
{
  assert(sbdi && pio && mt && cache);
  memset(sbdi, 0, sizeof(sbdi_t));
  sbdi->pio = pio;
  sbdi->crypto = NULL;
  sbdi->mt = mt;
  sbdi->cache = cache;
  sbdi->write_store[0].data = &sbdi->write_store_dat[0];
  sbdi->write_store[1].data = &sbdi->write_store_dat[1];
}

//----------------------------------------------------------------------
sbdi_t *sbdi_create(sbdi_pio_t *pio)
{
  sbdi_t *sbdi = calloc(1, sizeof(sbdi_t));
  if (!sbdi) {
    return NULL;
  }
  mt_t *mt = mt_create();
  if (!mt) {
    free(sbdi);
    return NULL;
  }
  sbdi_bc_t *cache = sbdi_bc_cache_create(sbdi, &sbdi_bl_sync,
      &sbdi_blic_is_phy_dat_in_phy_mngt_scope);
  if (!cache) {
    mt_delete(mt);
    free(sbdi);
    return NULL;
  }
  sbdi_init(sbdi, pio, mt, cache);
  return sbdi;
}

//----------------------------------------------------------------------
static inline void sbdi_crypto_destroy(sbdi_crypto_t *crypto,
    sbdi_hdr_v1_t *hdr)
{
  assert((!crypto && !hdr) || (crypto && hdr));
  if (crypto && hdr) {
    switch (hdr->type) {
    case SBDI_HDR_KEY_TYPE_INVALID:
      break;
    case SBDI_HDR_KEY_TYPE_NONE:
      sbdi_nocrypto_destroy(crypto);
      break;
    case SBDI_HDR_KEY_TYPE_SIV:
      sbdi_siv_destroy(crypto);
      break;
    case SBDI_HDR_KEY_TYPE_OCB:
      sbdi_ocb_destroy(crypto);
      break;
    case SBDI_HDR_KEY_TYPE_HMAC:
      sbdi_hmac_destroy(crypto);
      break;
    }
  }
}

//----------------------------------------------------------------------
void sbdi_delete(sbdi_t *sbdi)
{
  if (!sbdi) {
    return;
  }
  sbdi_bc_cache_destroy(sbdi->cache);
  mt_delete(sbdi->mt);
  sbdi_crypto_destroy(sbdi->crypto, sbdi->hdr);
  // Overwrite header if present
  sbdi_hdr_v1_delete(sbdi->hdr);
  memset(sbdi, 0, sizeof(sbdi_t));
  free(sbdi);
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_open(sbdi_t **s, sbdi_pio_t *pio, sbdi_crypto_type_t ct,
    sbdi_sym_mst_key_t mkey, mt_hash_t root)
{
  SBDI_CHK_PARAM(s && pio && mkey);
#ifdef SBDI_CRYPTO_TYPE
  ct = SBDI_CRYPTO_TYPE;
#endif
  // variables that need explicit cleaning
  siv_ctx mctx;
  memset(&mctx, 0, sizeof(siv_ctx));
  sbdi_hdr_v1_sym_key_t key;
  memset(&key, 0, sizeof(sbdi_hdr_v1_sym_key_t));
  sbdi_error_t r = SBDI_ERR_UNSPECIFIED;
  // Start body of function
  sbdi_t *sbdi = NULL;
  int cr = siv_init(&mctx, mkey, SIV_256);
  if (cr == -1) {
    r = SBDI_ERR_CRYPTO_FAIL;
    goto FAIL;
  }
  sbdi = sbdi_create(pio);
  if (!sbdi) {
    goto FAIL;
  }
  r = sbdi_hdr_v1_read(sbdi, &mctx);
  if (r == SBDI_ERR_IO_MISSING_BLOCK) {
    // Empty block device ==> create header
    uint8_t nonce[SBDI_HDR_V1_KEY_MAX_SIZE];
    pio->genseed(nonce, SBDI_HDR_V1_KEY_MAX_SIZE);
    sbdi_hdr_v1_derive_key(&mctx, key, nonce, SBDI_HDR_V1_KEY_MAX_SIZE/2,
        nonce + SBDI_HDR_V1_KEY_MAX_SIZE/2, SBDI_HDR_V1_KEY_MAX_SIZE/2);
    // For now we only support SIV
    sbdi_hdr_v1_key_type_t ktype = SBDI_HDR_KEY_TYPE_INVALID;
    switch (ct) {
    case SBDI_CRYPTO_NONE:
      r = sbdi_nocrypto_create(&sbdi->crypto, key);
      ktype = SBDI_HDR_KEY_TYPE_NONE;
      break;
    case SBDI_CRYPTO_SIV:
      r = sbdi_siv_create(&sbdi->crypto, key);
      if (r != SBDI_SUCCESS) {
        goto FAIL;
      }
      ktype = SBDI_HDR_KEY_TYPE_SIV;
      break;
    case SBDI_CRYPTO_OCB:
      r = sbdi_ocb_create(&sbdi->crypto, key);
      if (r != SBDI_SUCCESS) {
        goto FAIL;
      }
      ktype = SBDI_HDR_KEY_TYPE_OCB;
      break;
    case SBDI_CRYPTO_HMAC:
      r = sbdi_hmac_create(&sbdi->crypto, key);
      if (r != SBDI_SUCCESS) {
        goto FAIL;
      }
      ktype = SBDI_HDR_KEY_TYPE_HMAC;
      break;
    default:
      ktype = SBDI_HDR_KEY_TYPE_INVALID;
      r = SBDI_ERR_UNSUPPORTED;
      goto FAIL;
    }
    r = sbdi_hdr_v1_create(&sbdi->hdr, ktype, key);
    if (r != SBDI_SUCCESS) {
      goto FAIL;
    }
    r = sbdi_hdr_v1_write(sbdi, &mctx);
    if (r != SBDI_SUCCESS) {
      // TODO additional error handling required!
      goto FAIL;
    }
    *s = sbdi;
    return SBDI_SUCCESS;
  } else if (r != SBDI_SUCCESS) {
    goto FAIL;
  }
  sbdi_bl_verify_block_layer(sbdi, root);
  *s = sbdi;
  return SBDI_SUCCESS;

  FAIL: memset(&mctx, 0, sizeof(siv_ctx));
  memset(key, 0, sizeof(sbdi_hdr_v1_sym_key_t));
  sbdi_delete(sbdi);
  return r;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_sync(sbdi_t *sbdi, sbdi_sym_mst_key_t mkey, mt_hash_t root)
{
  SBDI_CHK_PARAM(sbdi && mkey);
  siv_ctx mctx;
  memset(&mctx, 0, sizeof(siv_ctx));

  // TODO The cache and header sync must be atomic, do something about that
  sbdi_error_t r = SBDI_ERR_UNSPECIFIED;
  int cr = siv_init(&mctx, mkey, SIV_256);
  if (cr == -1) {
    r = SBDI_ERR_CRYPTO_FAIL;
    goto FAIL;
  }
  r = sbdi_hdr_v1_write(sbdi, &mctx);
  if (r != SBDI_SUCCESS) {
    // TODO Potentially partially written header! Additional error handling required!
    goto FAIL;
  }
  r = sbdi_bc_sync(sbdi->cache);
  if (r != SBDI_SUCCESS) {
    // TODO Potentially inconsistent state! Additional error handling required!
    goto FAIL;
  }
  if (root) {
    r = sbdi_mt_sbdi_err_conv(mt_get_root(sbdi->mt, root));
    if (r != SBDI_SUCCESS) {
      // this should not happen, because it should have failed earlier
      goto FAIL;
    }
  }
  return SBDI_SUCCESS;

  FAIL: memset(&mctx, 0, sizeof(siv_ctx));
  return r;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_close(sbdi_t *sbdi, sbdi_sym_mst_key_t mkey, mt_hash_t root)
{
  SBDI_CHK_PARAM(sbdi && mkey && root);
  sbdi_error_t r = sbdi_sync(sbdi, mkey, root);
  if (r != SBDI_SUCCESS) {
    goto FAIL;
  }

  sbdi_delete(sbdi);
  return SBDI_SUCCESS;

 FAIL:
  return r;
}

#define SBDI_MIN(A, B) ((A) > (B))?(B):(A)

/*!
 * \brief computes the minimum of the two given size_t values
 *
 * @param a the first input size_t value
 * @param b the second input size_t value
 * @return the minimum of a and b
 */
static inline size_t size_min(size_t a, size_t b)
{
  return SBDI_MIN(a, b);
}

/*!
 * \brief Checks if an addition of the two size_t parameters is overflow safe
 *
 * @param a[in] the first size_t parameter to check
 * @param b[in] the second size_t parameter to check
 * @return true if the addition is overflow safe; false otherwise
 */
static inline int os_add_size(const size_t a, const size_t b)
{
  return (a + b) >= size_min(a, b);
}

/*!
 * \brief Checks if an addition of the two uint32_t parameters is overflow
 * safe
 *
 * @param a[in] the first uint32_t parameter to check
 * @param b[in] the second uint32_t parameter to check
 * @return true if the addition is overflow safe; false otherwise
 */
static inline int os_add_uint32(const uint32_t a, const uint32_t b)
{
  return (a + b) >= SBDI_MIN(a, b);
}

/*
 * The following macros are taken from:
 * Catching Integer Overflows in C
 * http://www.fefe.de/intof.html
 */
#define __HALF_MAX_SIGNED(type) ((type)1 << (sizeof(type)*8-2))
#define __MAX_SIGNED(type) (__HALF_MAX_SIGNED(type) - 1 + __HALF_MAX_SIGNED(type))
#define __MIN_SIGNED(type) (-1 - __MAX_SIGNED(type))

#define __MIN(type) ((type)-1 < 1?__MIN_SIGNED(type):(type)0)
#define __MAX(type) ((type)~__MIN(type))

/*!
 * \brief Tests if the given off_t can be safely added to the given size_t
 *
 * This function checks if it is safe to add the given off_t b to the given
 * size_t a. Safe here means that the addition will not lead to an integer
 * overflow. If b is positive normal unsigned integer overflow checks apply.
 * If b is negative the function ensures the a + (-b) >= 0. Finally, this
 * function also checks if the result of the addition fits into an off_t
 * type.
 *
 * @param a[in] the size_t value to add to b
 * @param b[in] the off_t value to add to a
 * @return SBDI_SUCCESS if the two values can be added safely;
 *         SBDI_ERR_ILLEGAL_PARAM otherwise
 */
static inline sbdi_error_t os_add_off_size(const size_t a, const off_t b)
{
  assert(sizeof(size_t) == sizeof(off_t));
  if (b < 0) {
    // Integer overflow possible
    size_t min_abs;
    // (l)(l)abs(__MIN(off_t) is potentially not defined take care of this
    if (b == __MIN(off_t)) {
      min_abs = ((size_t) __MAX(off_t)) + 1;
    } else {
      min_abs = (-1 * b);
    }
    SBDI_CHK_PARAM(min_abs > a);
  } else {
    // Both are positive ==> treat as unsigned integer overflow problem
    SBDI_CHK_PARAM(os_add_size(a, (size_t )b));
  }
  // Finally this checks if the result of the addition fits into an offset
  // type. TODO: this should map to an EOVERFLOW error instead of EINVAL -
  // use a different error code?
  SBDI_CHK_PARAM(a + b <= __MAX(off_t));
  return SBDI_SUCCESS;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_pread(ssize_t *rd, sbdi_t *sbdi, void *buf, size_t nbyte,
    off_t offset)
{
  SBDI_CHK_PARAM(rd && sbdi && buf);
  // Make sure offset is non-negative and less than or equal to the max sbd size
  SBDI_CHK_PARAM(offset >= 0 && offset <= SBDI_SIZE_MAX);
  assert(sizeof(size_t) == sizeof(off_t));
  SBDI_CHK_PARAM(nbyte < SBDI_SIZE_MAX);
//  SBDI_CHK_PARAM(__MAX(off_t) <= SBDI_SIZE_MAX);
  // nbyte > ssize_t ==> impl. defined
  SBDI_CHK_PARAM(nbyte <= __MAX(off_t));
  if (nbyte == 0) {
    *rd = 0;
    return SBDI_SUCCESS;
  }
  uint8_t *ptr = buf;
  size_t rlen = nbyte;
  size_t sbdi_size = sbdi_hdr_v1_get_size(sbdi);
  // Check if this will start reading beyond the secure block device
  if (offset >= sbdi_size) {
    *rd = 0;
    return SBDI_SUCCESS;
  }
  // We already asserted that the offset is positive ==> check if it will
  // overflow on addition using unsigned integer overflow check!
  SBDI_CHK_PARAM(os_add_size((size_t )offset, nbyte));
  if (offset + nbyte > sbdi_size) {
    rlen -= ((offset + nbyte) - sbdi_size);
  }
  // determine number of first block
  uint32_t idx = (offset == 0) ? (0) : (offset / SBDI_BLOCK_SIZE);
  uint32_t adr = (offset == 0) ? (0) : (offset % SBDI_BLOCK_SIZE);
  *rd = 0;
  size_t to_read =
      ((rlen + adr) > SBDI_BLOCK_SIZE) ? (SBDI_BLOCK_SIZE - adr) : rlen;
  while (rlen) {
    // TODO Testcase for writing past a block boundary
    SBDI_ERR_CHK(sbdi_bl_read_data_block(sbdi, ptr, idx, adr, to_read));
    *rd += to_read;
    rlen -= to_read;
    ptr += to_read;
    assert(os_add_uint32(idx, 1));
    idx += 1;
    // Block relative offset only relevant the first time.
    adr = 0;
    to_read = (rlen > SBDI_BLOCK_SIZE) ? SBDI_BLOCK_SIZE : rlen;
  }
  return SBDI_SUCCESS;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_pwrite(ssize_t *wr, sbdi_t *sbdi, const void *buf,
    size_t nbyte, off_t offset)
{
  SBDI_CHK_PARAM(wr && sbdi && buf);
  // Make sure offset is non-negative and less than or equal to the max SBD size
  SBDI_CHK_PARAM(offset >= 0 && offset <= SBDI_SIZE_MAX);
  assert(sizeof(size_t) == sizeof(off_t));
  SBDI_CHK_PARAM(nbyte < SBDI_SIZE_MAX);
//  SBDI_CHK_PARAM(__MAX(off_t) <= SBDI_SIZE_MAX);
  // nbyte > ssize_t ==> impl. defined ==> fail
  SBDI_CHK_PARAM(nbyte <= __MAX(off_t));
  if (nbyte == 0) {
    *wr = 0;
    return SBDI_SUCCESS;
  }
  uint8_t *ptr = (uint8_t *) buf;
  size_t rlen = nbyte;
  SBDI_CHK_PARAM(os_add_size((size_t )offset, nbyte));
  if ((offset + nbyte) > SBDI_SIZE_MAX) {
    // Function ensures offset less than SBDI_SIZE_MAX
    rlen = SBDI_SIZE_MAX - offset;
  }
  // determine number of first block
  uint32_t idx = (offset == 0) ? (0) : (offset / SBDI_BLOCK_SIZE);
  uint32_t adr = (offset == 0) ? (0) : (offset % SBDI_BLOCK_SIZE);
  *wr = 0;
  size_t to_write =
      ((rlen + adr) > SBDI_BLOCK_SIZE) ? (SBDI_BLOCK_SIZE - adr) : rlen;
  while (rlen) {
    // TODO Testcase for writing past a block boundary
    SBDI_ERR_CHK(sbdi_bl_write_data_block(sbdi, ptr, idx, adr, to_write));
    *wr += to_write;
    // The following addition depends on a previous os_add_size((size_t )offset, nbyte) check!
    if (offset + (*wr) > sbdi_hdr_v1_get_size(sbdi)) {
      sbdi_hdr_v1_update_size(sbdi, offset + (*wr));
    }
    rlen -= to_write;
    ptr += to_write;
    assert(os_add_uint32(idx, 1));
    idx += 1;
    // Block relative offset only relevant the first time.
    adr = 0;
    to_write = (rlen > SBDI_BLOCK_SIZE) ? SBDI_BLOCK_SIZE : rlen;
  }
  return SBDI_SUCCESS;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_lseek(off_t *new_off, sbdi_t *sbdi, off_t offset,
    sbdi_whence_t whence)
{
  SBDI_CHK_PARAM(new_off && sbdi && offset < SBDI_SIZE_MAX);
  size_t sbdi_size = sbdi_hdr_v1_get_size(sbdi);
  switch (whence) {
  case SBDI_SEEK_SET:
    // Disallow setting the offset to a negative number
    SBDI_CHK_PARAM(offset >= 0);
    sbdi->offset = offset;
    *new_off = sbdi->offset;
    return SBDI_SUCCESS;
  case SBDI_SEEK_CUR:
    // TODO write test case to test overflow protection
    SBDI_ERR_CHK(os_add_off_size(sbdi->offset, offset));
    SBDI_CHK_PARAM(sbdi->offset + offset < SBDI_SIZE_MAX);
    sbdi->offset += offset;
    *new_off = sbdi->offset;
    return SBDI_SUCCESS;
  case SBDI_SEEK_END:
    // TODO write test case to test overflow protection
    SBDI_ERR_CHK(os_add_off_size(sbdi_size, offset));
    SBDI_CHK_PARAM(sbdi->offset + offset < SBDI_SIZE_MAX);
    sbdi->offset = sbdi_size + offset;
    *new_off = sbdi->offset;
    return SBDI_SUCCESS;
  default:
    return SBDI_ERR_ILLEGAL_PARAM;
  }
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_read(ssize_t *rd, sbdi_t *sbdi, void *buf, size_t nbyte)
{
  SBDI_CHK_PARAM(sbdi);
  sbdi_error_t r = sbdi_pread(rd, sbdi, buf, nbyte, sbdi->offset);
  if (r != SBDI_SUCCESS && *rd == 0) {
    return r;
  }
  SBDI_ERR_CHK(os_add_off_size(sbdi->offset, *rd));
  sbdi->offset += *rd;
  return r;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_write(ssize_t *wr, sbdi_t *sbdi, const void *buf,
    size_t nbyte)
{
  SBDI_CHK_PARAM(sbdi);
  sbdi_error_t r = sbdi_pwrite(wr, sbdi, buf, nbyte, sbdi->offset);
  if (r != SBDI_SUCCESS && *wr == 0) {
    return r;
  }
  SBDI_ERR_CHK(os_add_off_size(sbdi->offset, *wr));
  sbdi->offset += *wr;
  return r;
}

//----------------------------------------------------------------------
sbdi_error_t sbdi_fsync(sbdi_t *sbdi, sbdi_sym_mst_key_t mkey)
{
#ifdef DEPRECATED
  // TODO The cache and header sync must be atomic, do something about that
  SBDI_CHK_PARAM(sbdi && mkey);
  SBDI_ERR_CHK(sbdi_bc_sync(sbdi->cache));
  siv_ctx mctx;
  memset(&mctx, 0, sizeof(siv_ctx));
  int cr = siv_init(&mctx, mkey, SIV_256);
  if (cr == -1) {
    sbdi_error_t r = SBDI_ERR_CRYPTO_FAIL;
    memset(&mctx, 0, sizeof(siv_ctx));
    return r;
  }
  sbdi_error_t r = sbdi_hdr_v1_write(sbdi, &mctx);
  memset(&mctx, 0, sizeof(siv_ctx));
  return r;
#else
  return sbdi_sync(sbdi, mkey, NULL);
#endif
}
