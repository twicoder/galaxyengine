/*
 * Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

#ifndef PLUGIN_X_SRC_QUERY_FORMATTER_H_
#define PLUGIN_X_SRC_QUERY_FORMATTER_H_

#include <stdint.h>
#include <string.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "plugin/x/ngs/include/ngs/memory.h"
#include "plugin/x/src/helper/to_string.h"

#include "plugin/x/src/galaxy_identifier.h"

struct CHARSET_INFO;

namespace xpl {

class Query_formatter {
 public:
  Query_formatter(ngs::PFS_string &query, CHARSET_INFO &charser);

  template <typename Value_type>
  class No_escape {
   public:
    No_escape(const Value_type &value) : m_value(value) {}

    const Value_type &m_value;
  };

  Query_formatter &operator%(const char *value);
  Query_formatter &operator%(const No_escape<const char *> &value);
  Query_formatter &operator%(const std::string &value);
  Query_formatter &operator%(const No_escape<std::string> &value);

  /** Galaxy X-protocol */
  Query_formatter &operator%(const gx::Identifier &identifier);

  template <typename Value_type>
  Query_formatter &operator%(const Value_type &value) {
    return put(value);
  }

  std::size_t count_tags() const;

 private:
  template <typename Value_type>
  Query_formatter &put(const Value_type &value) {
    validate_next_tag();
    std::string string_value = to_string(value);
    put_value(string_value.c_str(), string_value.length());

    return *this;
  }

  Query_formatter &put(const bool value) {
    validate_next_tag();
    std::string string_value = value ? "true" : "false";
    put_value(string_value.c_str(), string_value.length());

    return *this;
  }

  template <typename Value_type>
  Query_formatter &put_fp(const Value_type &value) {
    std::stringstream stream;
    validate_next_tag();
    stream << std::setprecision(std::numeric_limits<Value_type>::max_digits10)
           << value;
    std::string string_value = stream.str();
    put_value(string_value.c_str(), string_value.length());

    return *this;
  }

  void put_value(const char *value, const std::size_t length);
  void put_value_and_escape(const char *value, const std::size_t length);
  void validate_next_tag();

  /** Galaxy X-protocol */
  void put_ident_and_escape(const char *value, const std::size_t length);

  ngs::PFS_string &m_query;
  CHARSET_INFO &m_charset;
  std::size_t m_last_tag_position;
};

template <>
inline Query_formatter &Query_formatter::operator%
    <double>(const double &value) {
  return put_fp(value);
}

template <>
inline Query_formatter &Query_formatter::operator%<float>(const float &value) {
  return put_fp(value);
}

template <>
inline Query_formatter &Query_formatter::put<bool>(const bool &value) {
  validate_next_tag();
  const std::string string_value = value ? "true" : "false";
  put_value(string_value.c_str(), string_value.length());
  return *this;
}

}  // namespace xpl

#endif  // PLUGIN_X_SRC_QUERY_FORMATTER_H_
