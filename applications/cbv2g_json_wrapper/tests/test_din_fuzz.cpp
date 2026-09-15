/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Pionix GmbH and Contributors to EVerest
 *
 * test_din_fuzz.cpp - Replay the fuzzy-exi-generator CSV against the
 * wrapper. The CSV is produced by EVerest/fuzzy-exi-generator (mirror at
 * https://codeberg.org/sebalukas/fuzzy-exi-generator) and contains
 * EXICodec.jar-encoded EXI streams paired with their canonical JSON.
 *
 * Each CSV row spawns two parameterised GTest cases:
 *   1) decode(reference-EXI) structurally equals the reference JSON Body
 *   2) decode(encode(reference-JSON)) structurally equals the reference
 *      JSON Body  (encoded bytes are not compared to the reference --
 *      EXI permits multiple valid encodings of the same Infoset).
 *
 * Opt-in: only runs when CBV2G_FUZZ_CSV=/path/to/din.csv is set.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "cJSON.h"
#include "cbv2g_json_wrapper.h"

namespace {

struct CJsonOwner {
    cJSON* ptr{nullptr};
    explicit CJsonOwner(cJSON* p) : ptr(p) {
    }
    ~CJsonOwner() {
        if (ptr != nullptr) {
            cJSON_Delete(ptr);
        }
    }
    CJsonOwner(const CJsonOwner&) = delete;
    CJsonOwner& operator=(const CJsonOwner&) = delete;
};

struct Row {
    std::string message;
    std::vector<uint8_t> exi;
    std::string json;
    std::size_t line_no{0};
};

bool HexDecode(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) {
        return false;
    }
    out.clear();
    out.reserve(hex.size() / 2);
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = nib(hex[i]);
        int lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

/* RFC-4180-lite CSV row splitter. pandas.to_csv uses default quoting:
 * fields containing commas/quotes/newlines are double-quoted, and
 * embedded quotes are doubled. Multi-line records are not produced by
 * the generator, so we treat a row as a single line. */
std::vector<std::string> SplitCsvRow(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == ',') {
                fields.push_back(std::move(cur));
                cur.clear();
            } else if (c == '"' && cur.empty()) {
                in_quotes = true;
            } else {
                cur += c;
            }
        }
    }
    fields.push_back(std::move(cur));
    return fields;
}

std::vector<Row> LoadCsv(const std::string& path) {
    std::vector<Row> rows;
    std::ifstream in(path);
    if (!in) {
        return rows;
    }
    std::string line;
    std::size_t lineno = 0;
    if (!std::getline(in, line)) {
        return rows;
    }
    ++lineno; /* header */
    while (std::getline(in, line)) {
        ++lineno;
        if (line.empty()) {
            continue;
        }
        auto fields = SplitCsvRow(line);
        if (fields.size() < 3) {
            continue;
        }
        Row r;
        r.message = fields[0];
        if (!HexDecode(fields[1], r.exi)) {
            continue;
        }
        r.json = fields[2];
        r.line_no = lineno;
        rows.push_back(std::move(r));
    }
    return rows;
}

/* Compare the Body sub-tree of two V2G_Message envelopes. The SessionID
 * inside Header is randomised per generated row; the wrapper's job is
 * the Body grammar, which is what we assert here. */
::testing::AssertionResult BodyEqual(cJSON* expected, cJSON* actual) {
    cJSON* env_e = cJSON_GetObjectItemCaseSensitive(expected, "V2G_Message");
    cJSON* env_a = cJSON_GetObjectItemCaseSensitive(actual, "V2G_Message");
    if (env_e == nullptr || env_a == nullptr) {
        return ::testing::AssertionFailure() << "V2G_Message envelope missing";
    }
    cJSON* body_e = cJSON_GetObjectItemCaseSensitive(env_e, "Body");
    cJSON* body_a = cJSON_GetObjectItemCaseSensitive(env_a, "Body");
    if (body_e == nullptr || body_a == nullptr) {
        return ::testing::AssertionFailure() << "Body missing";
    }
    if (cJSON_Compare(body_e, body_a, /*case_sensitive=*/1) != 0) {
        return ::testing::AssertionSuccess();
    }
    char* se = cJSON_PrintUnformatted(body_e);
    char* sa = cJSON_PrintUnformatted(body_a);
    auto fail = ::testing::AssertionFailure()
                << "\n  expected Body: " << (se ? se : "<null>") << "\n  actual Body:   " << (sa ? sa : "<null>");
    if (se != nullptr) {
        free(se);
    }
    if (sa != nullptr) {
        free(sa);
    }
    return fail;
}

const char* CsvPath() {
    const char* p = std::getenv("CBV2G_FUZZ_CSV");
    return (p != nullptr && *p != '\0') ? p : nullptr;
}

std::vector<Row> LoadedRows() {
    static const std::vector<Row> rows = []() -> std::vector<Row> {
        const char* p = CsvPath();
        if (p == nullptr) {
            return {};
        }
        return LoadCsv(p);
    }();
    return rows;
}

} // namespace

class DinFuzzReplay : public ::testing::TestWithParam<Row> {};

TEST_P(DinFuzzReplay, DecodeMatchesEXICodecJar) {
    const Row& r = GetParam();
    std::vector<char> json_buf(8 * 1024, 0);
    int rc = cbv2g_decode(r.exi.data(), r.exi.size(), NS_DIN_MSG_DEF, json_buf.data(), json_buf.size());
    ASSERT_EQ(rc, CBV2G_SUCCESS) << "decode failed for " << r.message << " (csv line " << r.line_no << ")";

    CJsonOwner ours(cJSON_Parse(json_buf.data()));
    CJsonOwner ref(cJSON_Parse(r.json.c_str()));
    ASSERT_NE(ours.ptr, nullptr) << "wrapper produced unparseable JSON for " << r.message;
    ASSERT_NE(ref.ptr, nullptr) << "reference JSON unparseable on csv line " << r.line_no;

    EXPECT_TRUE(BodyEqual(ref.ptr, ours.ptr)) << " message=" << r.message << " csv_line=" << r.line_no;
}

TEST_P(DinFuzzReplay, EncodeThenDecodeIsSelfConsistent) {
    const Row& r = GetParam();
    std::vector<uint8_t> exi_buf(4 * 1024, 0);
    std::size_t exi_len = 0;
    int rc = cbv2g_encode(r.json.c_str(), NS_DIN_MSG_DEF, exi_buf.data(), exi_buf.size(), &exi_len);
    ASSERT_EQ(rc, CBV2G_SUCCESS) << "encode failed for " << r.message << " (csv line " << r.line_no << ")";
    ASSERT_GT(exi_len, 0u);

    std::vector<char> json_buf(8 * 1024, 0);
    rc = cbv2g_decode(exi_buf.data(), exi_len, NS_DIN_MSG_DEF, json_buf.data(), json_buf.size());
    ASSERT_EQ(rc, CBV2G_SUCCESS);

    CJsonOwner roundtrip(cJSON_Parse(json_buf.data()));
    CJsonOwner original(cJSON_Parse(r.json.c_str()));
    ASSERT_NE(roundtrip.ptr, nullptr);
    ASSERT_NE(original.ptr, nullptr);

    EXPECT_TRUE(BodyEqual(original.ptr, roundtrip.ptr)) << " message=" << r.message << " csv_line=" << r.line_no;
}

INSTANTIATE_TEST_SUITE_P(DinFuzz, DinFuzzReplay, ::testing::ValuesIn(LoadedRows()),
                         [](const ::testing::TestParamInfo<Row>& info) {
                             return info.param.message + "_L" + std::to_string(info.param.line_no);
                         });

TEST(DinFuzzMeta, ReportsCsvSource) {
    const char* p = CsvPath();
    if (p == nullptr) {
        GTEST_SKIP() << "Set CBV2G_FUZZ_CSV=/path/to/din.csv to enable fuzz replay.";
    }
    std::ifstream in(p);
    ASSERT_TRUE(in.good()) << "cannot open CBV2G_FUZZ_CSV=" << p;
    std::printf("[fuzz] loaded %zu rows from %s\n", LoadedRows().size(), p);
}
