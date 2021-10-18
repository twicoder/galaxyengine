// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/log_writer.h"

#include <stdint.h>
#include "util/coding.h"
#include "util/concurrent_direct_file_writer.h"
#include "util/crc32c.h"
#include "util/file_reader_writer.h"
#include "xengine/env.h"
#include "xengine/xengine_constants.h"

using namespace xengine;
using namespace util;
using namespace common;

namespace xengine {
namespace db {
namespace log {

Writer::Writer(util::ConcurrentDirectFileWriter *dest,
               uint64_t log_number, bool recycle_log_files,
               bool use_allocator)
    : dest_(dest),
      block_offset_(0),
      log_number_(log_number),
      recycle_log_files_(recycle_log_files),
      use_allocator_(use_allocator) {
  for (int i = 0; i <= kMaxRecordType; i++) {
    char t = static_cast<char>(i);
    type_crc_[i] = crc32c::Value(&t, 1);
  }
}
// for  compatibility  we leave this constructor here
Writer::Writer(unique_ptr<WritableFileWriter>&& dest, uint64_t log_number,
               bool recycle_log_files)
    : block_offset_(0),
      log_number_(log_number),
      recycle_log_files_(recycle_log_files) {
  for (int i = 0; i <= kMaxRecordType; i++) {
    char t = static_cast<char>(i);
    type_crc_[i] = crc32c::Value(&t, 1);
  }
}

Writer::~Writer() {
  if (nullptr != dest_) {
    if (dest_->use_allocator()) {
      dest_->~ConcurrentDirectFileWriter();
      dest_ = nullptr;
    } else {
      MOD_DELETE_OBJECT(ConcurrentDirectFileWriter, dest_);
    }
  }
}

void Writer::delete_file_writer(memory::SimpleAllocator *arena) {
  if (nullptr != dest_) {
    dest_->delete_write_file(arena);
    if (nullptr != arena) {
      FREE_OBJECT(ConcurrentDirectFileWriter, *arena, dest_);
    } else {
      MOD_DELETE_OBJECT(ConcurrentDirectFileWriter, dest_);
    }
  }
}

Status Writer::AddRecord(const Slice& slice) {
  return AddRecord(slice, calculate_crc(slice));
}

Status Writer::AddRecord(const Slice& slice, uint32_t crc) {
  return add_record_with_crc(slice, crc);
}

Status Writer::add_record_with_crc(const Slice& slice, uint32_t crc) {
  const char* ptr = slice.data();
  size_t left = slice.size();

  // Header size varies depending on whether we are recycling or not.
  const int header_size =
      recycle_log_files_ ? kRecyclableHeaderSize : kHeaderSize;

  // Fragment the record if necessary and emit it.  Note that if slice
  // is empty, we still want to iterate once to emit a single
  // zero-length record
  Status s;
  bool begin = true;
  do {
    const int64_t leftover = kBlockSize - block_offset_;
    assert(leftover >= 0);
    if (leftover < header_size) {
      // Switch to a new block
      if (leftover > 0) {
        // Fill the trailer (literal below relies on kHeaderSize and
        // kRecyclableHeaderSize being <= 11)
        assert(header_size <= 11);
        dest_->append(
            Slice("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", leftover));
      }
      block_offset_ = 0;
    }

    // Invariant: we never leave < header_size bytes in a block.
    assert(static_cast<int64_t>(kBlockSize - block_offset_) >= header_size);

    const size_t avail = kBlockSize - block_offset_ - header_size;
    const size_t fragment_length = (left < avail) ? left : avail;

    RecordType type;
    const bool end = (left == fragment_length);
    if (begin && end) {
      type = recycle_log_files_ ? kRecyclableFullType : kFullType;
    } else if (begin) {
      type = recycle_log_files_ ? kRecyclableFirstType : kFirstType;
    } else if (end) {
      type = recycle_log_files_ ? kRecyclableLastType : kLastType;
    } else {
      type = recycle_log_files_ ? kRecyclableMiddleType : kMiddleType;
    }

    s = EmitPhysicalRecord(type, ptr, fragment_length, crc);
    ptr += fragment_length;
    left -= fragment_length;
    begin = false;
  } while (s.ok() && left > 0);
  return s;
}

Status Writer::EmitPhysicalRecord(RecordType t, const char* ptr, size_t n,
                                  uint32_t crc) {
  assert(n <= 0xffff);  // Must fit in two bytes

  size_t header_size;
  char buf[kRecyclableHeaderSize];

  // Format the header
  buf[4] = static_cast<char>(n & 0xff);
  buf[5] = static_cast<char>(n >> 8);
  buf[6] = static_cast<char>(t);

  // uint32_t crc = type_crc_[t];
  if (t < kRecyclableFullType) {
    // Legacy record format
    assert(block_offset_ + kHeaderSize + n <= kBlockSize);
    header_size = kHeaderSize;
  } else {
    // Recyclable record format
    assert(block_offset_ + kRecyclableHeaderSize + n <= kBlockSize);
    header_size = kRecyclableHeaderSize;

    // Only encode low 32-bits of the 64-bit log number.  This means
    // we will fail to detect an old record if we recycled a log from
    // ~4 billion logs ago, but that is effectively impossible, and
    // even if it were we'dbe far more likely to see a false positive
    // on the 32-bit CRC.
    EncodeFixed32(buf + 7, static_cast<uint32_t>(log_number_));
    // crc = crc32c::Extend(crc, buf + 7, 4);
  }

  EncodeFixed32(buf, crc);

  // Write the header and the payload
  int ret = dest_->append(Slice(buf, header_size), Slice(ptr, n));
  if (0 == ret) {
    block_offset_ += header_size + n;
    return Status::OK();
  }
  return Status::IOError();
}

}  // namespace log
}
}  // namespace xengine