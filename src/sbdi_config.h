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
/// \brief Specifies global data types.
///
#ifndef SBDI_CONFIG_H_
#define SBDI_CONFIG_H_

#include "config.h"
#include "sbdi_err.h"

#include <stdint.h>
#include <assert.h>
#include <stddef.h>

#define SBDI_BLOCK_INDEX_INVALID UINT32_MAX

/*!
 * \brief the tag data type for the integrity tags used by the secure block
 * device interface
 */
typedef uint8_t sbdi_tag_t[SBDI_BLOCK_TAG_SIZE];

/*!
 * \brief the packed representation of the counter used to make every
 * encryption unique
 *
 * The runtime representation of the the counter might differ from its data
 * at rest state. This data type defines how much space a packed counter
 * needs.
 */
typedef uint8_t sbdi_ctr_pkd_t[SBDI_BLOCK_CTR_SIZE];

/*!
 * \brief the block data data type for storing actual block data
 */
typedef uint8_t sbdi_bl_data_t[SBDI_BLOCK_SIZE];

typedef struct secure_block_device_interface sbdi_t;

/*!
 * \brief the basic block data type combining a block index with the block
 * data
 *
 * The block itself does not distinguish between physical block indexes and
 * logical block indices. The user of this data type has to take care of the
 * semantic of the index.
 */
typedef struct sbdi_block {
  uint32_t idx;         //!< the block index
  sbdi_bl_data_t *data; //!< a pointer to the actual block data
} sbdi_block_t;

/*!
 * \brief Initializes a secure block device interface block with the given
 * block index and block data pointer
 *
 * This function is purely for initialization, the block needs to be
 * allocated first by the caller.
 *
 * @param blk[inout] a pointer to the block to initialize
 * @param blk_idx[in] the index of the block
 * @param blk_data[in] a pointer to the block data
 */
static inline void sbdi_block_init(sbdi_block_t *blk, const uint32_t blk_idx,
    sbdi_bl_data_t *blk_data)
{
  assert(blk && blk_idx < SBDI_BLOCK_INDEX_INVALID);
  blk->idx = blk_idx;
  blk->data = blk_data;
}

/*!
 * \brief Invalidates a given secure block device block by setting its block
 * index to SBDI_BLOCK_INDEX_INVALID
 * @param blk[in] a pointer to the block to invalidate
 */
static inline void sbdi_block_invalidate(sbdi_block_t *blk)
{
  assert(blk);
  blk->idx = SBDI_BLOCK_INDEX_INVALID;
  blk->data = NULL;
}

/*!
 * \brief Determines if the given physical block index is valid
 *
 * This function checks if the given physical block index value is less than
 * or equal to the maximum physical block index.
 *
 * @param phy[in] the physical block index value to check
 * @return true if the given physical block index value is less than or equal
 * to the maximum physical block index value; false otherwise
 */
static inline int sbdi_block_is_valid_phy(const uint32_t phy)
{
  return phy <= SBDI_BLK_MAX_PHY;
}

/*!
 * \brief Determines if the given logical block index is valid
 *
 * This function checks if the given logical block index value is less than
 * or equal to the maximum logical block index.
 *
 * @param log[in] the logical block index value to check
 * @return true if the given logical block index value is less than or equal
 * to the maximum logical block index value; false otherwise
 */
static inline int sbdi_block_is_valid_log(const uint32_t log)
{
  return log <= SBDI_BLK_MAX_PHY;
}

#endif /* SBDI_CONFIG_H_ */
