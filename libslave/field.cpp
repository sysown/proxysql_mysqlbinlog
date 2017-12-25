/* Copyright 2011 ZAO "Begun".
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <cstdio>
#include <inttypes.h>
#include <vector>
#include <stdexcept>
#include <mysql/my_global.h>
#undef min
#undef max

#include <mysql/m_string.h>

#include "dec_util.h"
#include "field.h"

#include "Logging.h"




namespace slave
{



Field_num::Field_num(const std::string& field_name_arg, const std::string& type):
    Field(field_name_arg, type) {}

Field_tiny::Field_tiny(const std::string& field_name_arg, const std::string& type):
    Field_num(field_name_arg, type) {}

const char* Field_tiny::unpack(const char* from) {

    char tmp = *((char*)(from));
    field_data = tmp;

    LOG_TRACE(log, "  tiny: " << (int)(tmp) << " // " << pack_length());

    return from + pack_length();
}


Field_short::Field_short(const std::string& field_name_arg, const std::string& type):
    Field_num(field_name_arg, type) {}

const char* Field_short::unpack(const char* from) {

    uint16 tmp = uint2korr(from);
    field_data = tmp;

    LOG_TRACE(log, "  short: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_medium::Field_medium(const std::string& field_name_arg, const std::string& type):
    Field_num(field_name_arg, type) {}

const char* Field_medium::unpack(const char* from) {

    uint32 tmp = uint3korr(from);
    field_data = tmp;

    LOG_TRACE(log, "  medium: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_long::Field_long(const std::string& field_name_arg, const std::string& type):
    Field_num(field_name_arg, type) {}

const char* Field_long::unpack(const char* from) {

    uint32 tmp = uint4korr(from);
    field_data = tmp;

    LOG_TRACE(log, "  long: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_longlong::Field_longlong(const std::string& field_name_arg, const std::string& type):
    Field_num(field_name_arg, type) {}

const char* Field_longlong::unpack(const char* from) {

    ulonglong tmp = uint8korr(from);
    field_data = tmp;

    LOG_TRACE(log, "  longlong: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_real::Field_real(const std::string& field_name_arg, const std::string& type):
    Field_num(field_name_arg, type) {}

Field_double::Field_double(const std::string& field_name_arg, const std::string& type):
    Field_real(field_name_arg, type) {}

const char* Field_double::unpack(const char* from) {

    double tmp = *((double*)(from));
    field_data = tmp;

    LOG_TRACE(log, "  double: " << tmp << " // " << pack_length());

    return from + pack_length();
}


Field_float::Field_float(const std::string& field_name_arg, const std::string& type):
    Field_real(field_name_arg, type) {}

const char* Field_float::unpack(const char* from) {

    float tmp = *((float*)(from));
    field_data = tmp;

    LOG_TRACE(log, "  float: " << tmp << " // " << pack_length());

    return from + pack_length();
}


Field_str::Field_str(const std::string& field_name_arg, const std::string& type):
    Field(field_name_arg, type) {}

Field_temporal::Field_temporal(const std::string &field_name_arg, const std::string &type, bool old_storage):
    Field_longstr(field_name_arg, type)
  , is_old_storage(old_storage) {}

Field_timestamp::Field_timestamp(const std::string& field_name_arg, const std::string& type, bool old_storage):
    Field_temporal(field_name_arg, type, old_storage) {

    reset(old_storage, true);
}

void Field_timestamp::reset(bool old_storage, bool ctor_call)
{
    if (!ctor_call && is_old_storage == old_storage)
        return;

    is_old_storage = old_storage;
    if (is_old_storage)
    {
        field_length = 4;
    }
    else
    {
        int digits = 0;
        sscanf(field_type.c_str(), "timestamp(%d)", &digits);
        switch (digits)
        {
        case 0:
            field_length = 4;
            break;
        case 1:
        case 2:
            field_length = 5;
            break;
        case 3:
        case 4:
            field_length = 6;
            break;
        case 5:
        case 6:
            field_length = 7;
            break;
        default:
            throw std::runtime_error(
                "Field_timestamp::reset(): unknown Fractional Seconds Precision in field '" + field_name + "': '" + field_type + "'."
            );
        }
    }
}

const char* Field_timestamp::unpack(const char* from) {

    uint32 tmp;
    if (is_old_storage)
    {
        tmp = uint4korr(from);
    }
    else
    {
        // !! we ignore fractional part
        // 4 bytes + fractional-seconds storage, big endian
        // same as before 5.6.4, except big endian rather than little endian

        tmp = 0;
        for (unsigned int i = 0; i < 4; ++i)
            *((unsigned char *)&tmp + 3 - i) = *(from + i);
    }

    field_data = tmp;

    LOG_TRACE(log, "  timestamp: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_year::Field_year(const std::string& field_name_arg, const std::string& type):
    Field_tiny(field_name_arg, type) {}

Field_datetime::Field_datetime(const std::string& field_name_arg, const std::string& type, bool old_storage):
    Field_temporal(field_name_arg, type, old_storage) {

    reset(old_storage, true);
}

void Field_datetime::reset(bool old_storage, bool ctor_call)
{
    if (!ctor_call && is_old_storage == old_storage)
        return;

    is_old_storage = old_storage;
    if (is_old_storage)
    {
        field_length = 8;
    }
    else
    {
        int digits = 0;
        sscanf(field_type.c_str(), "datetime(%d)", &digits);
        switch (digits)
        {
        case 0:
            field_length = 5;
            break;
        case 1:
        case 2:
            field_length = 6;
            break;
        case 3:
        case 4:
            field_length = 7;
            break;
        case 5:
        case 6:
            field_length = 8;
            break;
        default:
            throw std::runtime_error(
                "Field_datetime::reset(): unknown Fractional Seconds Precision in field '" + field_name + "': '" + field_type + "'."
            );
        }
    }
}

const char* Field_datetime::unpack(const char* from)
{
    ulonglong tmp;
    if (is_old_storage)
    {
        tmp = uint8korr(from);
    }
    else
    {
        // !! we ignore fractional part
        // 5 bytes + fractional-seconds storage, big endian
        // ---------------------------
        //  1 bit  sign           (1= non-negative, 0= negative)
        // 17 bits year*13+month  (year 0-9999, month 0-12)
        //  5 bits day            (0-31)
        //  5 bits hour           (0-23)
        //  6 bits minute         (0-59)
        //  6 bits second         (0-59)
        // ---------------------------
        // 40 bits = 5 bytes

        ulonglong data;
        for (unsigned int i = 0; i < 5; ++i)
            *((unsigned char *)&data + 4 - i) = *(from + i);

        tmp = data & 63;
        data >>= 6;
        tmp += (data & 63) * 100;
        data >>= 6;
        tmp += (data & 31) * 10000;
        data >>= 5;
        tmp += (data & 31) * 1000000;
        data >>= 5;

        ulonglong year_month = data & ((1 << 17) - 1);
        tmp += year_month % 13 * 100000000;
        tmp += year_month / 13 * 10000000000;
    }

    field_data = tmp;

    LOG_TRACE(log, "  datetime: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_date::Field_date(const std::string& field_name_arg, const std::string& type):
    Field_str(field_name_arg, type) {}

const char* Field_date::unpack(const char* from) {

    uint32 tmp = uint3korr(from);
    field_data = tmp;

    LOG_TRACE(log, "  date: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_time::Field_time(const std::string& field_name_arg, const std::string& type, bool old_storage):
    Field_temporal(field_name_arg, type, old_storage) {

    reset(old_storage, true);
}

void Field_time::reset(bool old_storage, bool ctor_call)
{
    if (!ctor_call && is_old_storage == old_storage)
        return;

    is_old_storage = old_storage;
    if (is_old_storage)
    {
        field_length = 3;
    }
    else
    {
        int digits = 0;
        sscanf(field_type.c_str(), "time(%d)", &digits);
        switch (digits)
        {
        case 0:
            field_length = 3;
            break;
        case 1:
        case 2:
            field_length = 4;
            break;
        case 3:
        case 4:
            field_length = 5;
            break;
        case 5:
        case 6:
            field_length = 6;
            break;
        default:
            throw std::runtime_error(
                "Field_time::reset(): unknown Fractional Seconds Precision in field '" + field_name + "': '" + field_type + "'."
            );
        }
    }
}

const char* Field_time::unpack(const char* from) {

    int32 tmp;
    if (is_old_storage)
    {
        tmp = sint3korr(from);
    }
    else
    {
        // !! we ignore fractional part
        // 3 bytes + fractional-seconds storage, big endian
        // ---------------------
        //  1 bit sign    (1= non-negative, 0= negative)
        //  1 bit unused  (reserved for future extensions)
        // 10 bits hour   (0-838)
        //  6 bits minute (0-59)
        //  6 bits second (0-59)
        // ---------------------
        // 24 bits = 3 bytes

        uint32 data = 0;
        for (unsigned int i = 0; i < 3; ++i)
            *((unsigned char *)&data + 2 - i) = *(from + i);

        uint32 sign = (uint32)((data & (1 << 23)) >> 23);
        if (sign == 0)
            data = (1 << 23) - data;
        tmp = data & 63;
        data >>= 6;
        tmp += (data & 63) * 100;
        data >>= 6;
        tmp += (data & 1023) * 10000;
        if (sign == 0)
            tmp = -tmp;
    }

    field_data = tmp;

    LOG_TRACE(log, "  time: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_enum::Field_enum(const std::string& field_name_arg, const std::string& type):
    Field_str(field_name_arg, type) {

    // for fields of type 'enum' the number of elements needs to be counted
    // if the number of elements is less than 255, then this type has a size of 1 byte
    count_elements = 1;
    for (std::string::const_iterator i = type.begin(); i != type.end(); ++i) {
        if (*i == ',') {
            count_elements++;
        }
    }
}

const char* Field_enum::unpack(const char* from) {

    int tmp;

    if (pack_length() == 1) {

        tmp = int(*((char*)(from)));

    } else {
        tmp = int(*((short*)(from)));
    }

    field_data = tmp;

    LOG_TRACE(log, "  enum: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_set::Field_set(const std::string& field_name_arg, const std::string& type):
    Field_enum(field_name_arg, type) {

    // for fields of type 'enum' the number of elements needs to be counted
    // the formula for determining size is (N+7)/8

    count_elements = 1;
    for (std::string::const_iterator i = type.begin(); i != type.end(); ++i) {
        if (*i == ',') {
            count_elements++;
        }
    }
}

const char* Field_set::unpack(const char* from) {
    ulonglong tmp;

    switch(pack_length()) {
    case 1:
        tmp = ulonglong(*((unsigned char*)(from)));
        break;
    case 2:
        tmp = ulonglong(uint2korr(from));
        break;
    case 3:
        tmp = ulonglong(uint3korr(from));
        break;
    case 4:
        tmp = ulonglong(uint4korr(from));
        break;
    case 8:
        tmp = uint8korr(from);
        break;
    default:
        tmp = uint8korr(from);
        break;
    }

    field_data = tmp;

    LOG_TRACE(log, "  set: " << tmp << " // " << pack_length());

    return from + pack_length();
}

Field_longstr::Field_longstr(const std::string& field_name_arg, const std::string& type):
    Field_str(field_name_arg, type)  {}

Field_varstring::Field_varstring(const std::string& field_name_arg, const std::string& type, const collate_info& collate):
    Field_longstr(field_name_arg, type) {

    // field size is determined by string type capacity

    std::string::size_type b = type.find('(', 0);
    std::string::size_type e = type.find(')', 0);

    if (b == std::string::npos || e == std::string::npos) {
        throw std::runtime_error("Field_string: Incorrect field VARCHAR");
    }

    std::string str_count(type, b+1, e-b-1);
    int symbols = atoi(str_count.c_str());
    int bytes = symbols * collate.maxlen;

    // number of bytes for holding the length
    length_bytes = ((bytes < 256) ? 1 : 2);

    // max length of string
    field_length = symbols;
}

const char* Field_varstring::unpack(const char* from) {

    unsigned length_row;
    if (length_bytes == 1) {
        //length_row = (unsigned int) (unsigned char) (*to = *from++);
        length_row = (unsigned int) (unsigned char) (*from++);

    } else {
        length_row = uint2korr(from);
        from++;
        from++;
    }

    std::string tmp(from, length_row);

    field_data = tmp;

    LOG_TRACE(log, "  varstr: '" << tmp << "' // " << length_bytes << " " << length_row);

    return from + length_row;
}


Field_blob::Field_blob(const std::string& field_name_arg, const std::string& type):
    Field_longstr(field_name_arg, type), packlength(2) {}

Field_tinyblob::Field_tinyblob(const std::string& field_name_arg, const std::string& type):
    Field_blob(field_name_arg, type) { packlength = 1; }

Field_mediumblob::Field_mediumblob(const std::string& field_name_arg, const std::string& type):
    Field_blob(field_name_arg, type) { packlength = 3; }

Field_longblob::Field_longblob(const std::string& field_name_arg, const std::string& type):
    Field_blob(field_name_arg, type) { packlength = 4; }

const char* Field_blob::unpack(const char* from) {

    const unsigned length_row = get_length(from);
    from += packlength;

    std::string tmp(from, length_row);

    field_data = tmp;

    LOG_TRACE(log, "  blob: '" << tmp << "' // " << packlength << " " << length_row);

    return from + length_row;
}


unsigned int Field_blob::get_length(const char *pos) {

    switch (packlength)
    {
    case 1:
        return (unsigned int) (unsigned char) pos[0];
    case 2:
    {

        unsigned short tmp = 0;

        /*
        if (db_low_byte_first) {
            tmp = sint2korr(pos);
        } else {
            shortget(tmp,pos);
        }
        */
        tmp = sint2korr(pos);

        return (unsigned int) tmp;

    }
    case 3:
        return (unsigned int) uint3korr(pos);
    case 4:
    {
        unsigned int tmp;

        /*
        if (db_low_byte_first)
            tmp = uint4korr(pos);
        else
            longget(tmp,pos);
        */
        tmp = uint4korr(pos);

        return (unsigned int) tmp;
    }

    }

    throw std::runtime_error("Oops, wrong packlength in Field_blob::get_length(): wanted 1, 2, 3 or 4.");
}

Field_decimal::Field_decimal(const std::string& field_name_arg, const std::string& type):
    Field_longstr(field_name_arg, type),
    intg(0),
    frac(0)
{
    // Get field sizes: decimal(M,D)
    // M - total number of digits, M-D - before comma

    const std::string::size_type b = type.find('(', 0);

    if (b == std::string::npos) {
        throw std::runtime_error("Field_string: Incorrect field DECIMAL");
    }

    int m, d;
    if (2 != sscanf(type.c_str() + b, "(%d,%d)", &m, &d) || m <= 0 || m < d) {
        throw std::runtime_error("Field_string: Incorrect field DECIMAL");
    }

    intg = m - d;
    frac = d;

    static const int dig2bytes[] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
    field_length = (intg / 9) * 4 + dig2bytes[intg % 9] + (frac / 9) * 4 + dig2bytes[frac % 9];
}

const char* Field_decimal::unpack(const char *from)
{
    double result = dec2double(from);
    field_data = result;
    return from + pack_length();
}

double Field_decimal::dec2double(const char* from)
{
    decimal_t val;
    val.len = intg + frac;
    size_t bytes = val.len * sizeof(decimal_digit_t);
    val.buf = (decimal_digit_t *)alloca(bytes);
    memset(val.buf, 0, bytes);

    dec_util::bin2dec(from, &val, intg+frac, frac);
    double v = 0;
    dec_util::dec2dbl(&val, &v);

    return v;
}


Field_bit::Field_bit(const std::string& field_name_arg, const std::string& type)
    : Field(field_name_arg, type)
{
    std::string::size_type b = type.find('(', 0);
    std::string::size_type e = type.find(')', 0);

    if (b == std::string::npos || e == std::string::npos)
        throw std::runtime_error("Field_bit: Incorrect field BIT");

    LOG_TRACE(log, " Field_bit: arg " << field_name_arg << " type " << type);

    std::string str_count(type, b+1, e-b-1);

    _pack_length = (::atoi(str_count.c_str()) + 7) / 8;

    if (0 == _pack_length || _pack_length > 8 )
        throw std::runtime_error("Field_bit: incorrect data length");
}

const char* Field_bit::unpack(const char *from)
{
    uint64_t value = 0;

    for (const char *b = from, *e = from + _pack_length; b < e; ++b)
    {
        value <<= 8;
        value |= *(const uint8*)b;
    }

    LOG_TRACE(log, "  bit: 0x" << std::hex << value);

    field_data = value;

    return from + _pack_length;
}

} // namespace slave
