package com.dudleylane.quickfix;

import java.math.BigDecimal;
import java.time.Instant;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeParseException;
import java.util.regex.Pattern;

/**
 * The conversions behind the generated field classes' set and get (com.dudleylane.quickfix.field). A field
 * is a plain value: set writes FieldMap.setField(tag, string), get reads FieldMap.getField(tag), and the
 * conversion happens here, in the engine's formats, with no native object per field -- measured 2.3-2.7x
 * faster than a SWIG field object per call (#148). A value that does not convert is the engine's
 * IncorrectDataFormat, naming the tag, as the C++ field classes throw.
 */
public final class FieldConvert {
    // FIX's float: an optional sign, digits, and an optional fraction -- never an exponent, NaN or infinity.
    private static final Pattern FLOAT = Pattern.compile("-?(\\d+\\.?\\d*|\\.\\d+)");

    private FieldConvert() {}

    public static String getString(FieldMap map, int tag) {
        return map.getField(tag);
    }

    public static void setString(FieldMap map, int tag, String value) {
        map.setField(tag, value);
    }

    public static int getInt(FieldMap map, int tag) {
        String value = map.getField(tag);
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            throw new IncorrectDataFormat(tag, value);
        }
    }

    public static void setInt(FieldMap map, int tag, int value) {
        map.setField(tag, Integer.toString(value));
    }

    public static long getLong(FieldMap map, int tag) {
        String value = map.getField(tag);
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            throw new IncorrectDataFormat(tag, value);
        }
    }

    public static void setLong(FieldMap map, int tag, long value) {
        map.setField(tag, Long.toString(value));
    }

    public static char getChar(FieldMap map, int tag) {
        String value = map.getField(tag);
        if (value.length() != 1) {
            throw new IncorrectDataFormat(tag, value);
        }
        return value.charAt(0);
    }

    public static void setChar(FieldMap map, int tag, char value) {
        map.setField(tag, String.valueOf(value));
    }

    public static boolean getBoolean(FieldMap map, int tag) {
        String value = map.getField(tag);
        if (value.equals("Y")) {
            return true;
        }
        if (value.equals("N")) {
            return false;
        }
        throw new IncorrectDataFormat(tag, value);
    }

    public static void setBoolean(FieldMap map, int tag, boolean value) {
        map.setField(tag, value ? "Y" : "N");
    }

    public static double getDouble(FieldMap map, int tag) {
        String value = map.getField(tag);
        if (!FLOAT.matcher(value).matches()) {
            throw new IncorrectDataFormat(tag, value);
        }
        return Double.parseDouble(value);
    }

    /** Written as the shortest decimal that reads back as value, never with an exponent: 1.0E7 is "10000000". */
    public static void setDouble(FieldMap map, int tag, double value) {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            throw new IllegalArgumentException("tag " + tag + ": FIX has no " + value);
        }
        map.setField(tag, formatDouble(value));
    }

    static String formatDouble(double value) {
        if (value == 0) {
            return "0";
        }
        return BigDecimal.valueOf(value).stripTrailingZeros().toPlainString();
    }

    // FIX's dates and times: yyyyMMdd, HH:mm:ss with 0, 3, 6 or 9 fractional digits, and the two joined by '-'.
    private static final DateTimeFormatter DATE = DateTimeFormatter.ofPattern("uuuuMMdd");
    private static final Pattern TIME = Pattern.compile("\\d{2}:\\d{2}:\\d{2}(\\.(\\d{3}|\\d{6}|\\d{9}))?");

    public static Instant getUtcTimestamp(FieldMap map, int tag) {
        String value = map.getField(tag);
        if (value.length() < 17 || value.charAt(8) != '-' || !TIME.matcher(value.substring(9)).matches()) {
            throw new IncorrectDataFormat(tag, value);
        }
        try {
            return LocalDateTime.of(LocalDate.parse(value.substring(0, 8), DATE), LocalTime.parse(value.substring(9)))
                .toInstant(ZoneOffset.UTC);
        } catch (DateTimeParseException e) {
            throw new IncorrectDataFormat(tag, value);
        }
    }

    /** In milliseconds, as the engine writes timestamps by default, or finer when value has finer. */
    public static void setUtcTimestamp(FieldMap map, int tag, Instant value) {
        LocalDateTime time = LocalDateTime.ofInstant(value, ZoneOffset.UTC);
        map.setField(tag, time.toLocalDate().format(DATE) + "-" + formatTime(time.toLocalTime()));
    }

    public static LocalDate getDate(FieldMap map, int tag) {
        String value = map.getField(tag);
        try {
            if (value.length() != 8) {
                throw new IncorrectDataFormat(tag, value);
            }
            return LocalDate.parse(value, DATE);
        } catch (DateTimeParseException e) {
            throw new IncorrectDataFormat(tag, value);
        }
    }

    public static void setDate(FieldMap map, int tag, LocalDate value) {
        map.setField(tag, value.format(DATE));
    }

    public static LocalTime getTimeOnly(FieldMap map, int tag) {
        String value = map.getField(tag);
        if (!TIME.matcher(value).matches()) {
            throw new IncorrectDataFormat(tag, value);
        }
        try {
            return LocalTime.parse(value);
        } catch (DateTimeParseException e) {
            throw new IncorrectDataFormat(tag, value);
        }
    }

    public static void setTimeOnly(FieldMap map, int tag, LocalTime value) {
        map.setField(tag, formatTime(value));
    }

    static String formatTime(LocalTime time) {
        int nanos = time.getNano();
        String digits = nanos % 1_000_000 == 0 ? String.format("%03d", nanos / 1_000_000)
                        : nanos % 1_000 == 0   ? String.format("%06d", nanos / 1_000)
                                               : String.format("%09d", nanos);
        return String.format("%02d:%02d:%02d.%s", time.getHour(), time.getMinute(), time.getSecond(), digits);
    }

    /** A group's field order for the Group constructor: the tags in order, then the terminating 0. */
    public static IntArray order(int... tags) {
        IntArray order = new IntArray(tags.length + 1);
        for (int i = 0; i < tags.length; ++i) {
            order.setitem(i, tags[i]);
        }
        order.setitem(tags.length, 0);
        return order;
    }
}
