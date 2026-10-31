# everest::date

Parsing and formatting of date-times for EVerest.

```cpp
#include <everest/date/date.hpp>

namespace datetime = everest::lib::datetime;

std::optional<datetime::utc_time_point> tp = datetime::parse_rfc3339("2026-01-02T05:04:05.678+02:00");
std::string text = datetime::to_rfc3339(*tp); // "2026-01-02T03:04:05.678Z"
```

Link `everest::date` in CMake or depend on `//lib/everest/date` in Bazel.

## Parsing

The parsers return `std::nullopt` for anything they do not accept. They never throw and never wrap around.

| Input | `parse_rfc3339` | `parse_rfc3339_lenient` |
|---|---|---|
| `YYYY-MM-DDTHH:MM:SS`, fixed width, `T` or `t` | required | required |
| Fraction `.frac` | yes | yes |
| Fraction `,frac` | no | yes |
| Offset `Z`, `z`, `+HH:MM`, `-HH:MM` | required | yes |
| Offset `+HHMM`, `+HH` | no | yes |
| No offset, read as UTC | no | yes |
| Anything after the offset | no | no |

`parse_rfc3339` follows [RFC 3339](https://www.rfc-editor.org/rfc/rfc3339), which OCPP 2.0.1 and 2.1 require.

`parse_rfc3339_lenient` accepts RFC 3339 plus the deviations in the table above, which ISO 8601 extended format allows.
OCPP 1.6 asks for ISO 8601 without naming a profile, while its JSON schemas declare timestamps as RFC 3339 `date-time`,
so its receivers should tolerate these deviations. It is no ISO 8601 parser: RFC 3339 and ISO 8601 overlap without
either containing the other, a missing offset is read as UTC where ISO 8601 would mean local time, and other ISO 8601
representations, such as the basic format (`20260102T030405Z`), ordinal and week dates or reduced precision, are
rejected.

Both parsers:

- check every field range, including the UTC offset (at most 23:59),
- keep fractional seconds to nanoseconds and ignore further digits,
- accept second 60 only for an actual leap second, after applying the UTC offset,
- reject time points outside the range of `utc_time_point`, a `date::utc_clock` time point with nanosecond
  resolution: 1677-09-21T00:12:43.145224192Z to 2262-04-11T23:46:49.854775807Z.

`parse_rfc3339_lenient_clamped` accepts what `parse_rfc3339_lenient` accepts, but clamps time points outside that range
to `utc_time_point::min()` or `utc_time_point::max()` instead of rejecting them. It is meant for peers that write far-off
dates such as `9999-12-31T23:59:59Z` to mean "never".

## Formatting

`to_rfc3339` writes UTC with millisecond precision, `YYYY-MM-DDTHH:MM:SS.mmmZ`, which satisfies the OCPP 2.x limit of
three fractional digits. It truncates towards zero and writes a leap second as second 60. Its output always parses back
with `parse_rfc3339`, including for `utc_time_point::min()` and `utc_time_point::max()`.
