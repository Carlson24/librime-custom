//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <fstream>
#include <map>
#include <gtest/gtest.h>
#include <rime/common.h>
#include <rime/algo/strings.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/dict_compiler.h>

namespace rime {

class AuxDictTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dict_.reset(new Dictionary("aux_dict_test", {},
                               {New<Table>(path{"aux_dict_test.table.bin"})},
                               New<Prism>(path{"aux_dict_test.prism.bin"})));
    dict_->Remove();
    DictCompiler dict_compiler(dict_.get());
    dict_compiler.set_options(DictCompiler::kDump);
    ASSERT_TRUE(dict_compiler.Compile(path()));
    dict_->Load();
  }

  void TearDown() override { dict_.reset(); }

  map<string, string> ReadDump() {
    map<string, string> codes;
    std::ifstream fin("aux_dict_test.table.txt");
    string line;
    while (getline(fin, line)) {
      if (line.empty() || line[0] == '#')
        continue;
      auto row = strings::split(line, "\t");
      if (row.size() < 2)
        continue;
      codes[row[0]] = row[1];
    }
    return codes;
  }

  the<Dictionary> dict_;
};

TEST_F(AuxDictTest, AuxiliaryCodesAppended) {
  auto codes = ReadDump();
  EXPECT_EQ("ni;re", codes["\xe4\xbd\xa0"]);   // 你
  EXPECT_EQ("hao;nz", codes["\xe5\xa5\xbd"]);  // 好
  EXPECT_EQ("zen;rx", codes["\xe6\x80\x8e"]);  // 怎
  EXPECT_EQ("me;", codes["\xe4\xb9\x88"]);     // 么 (no aux code)
  EXPECT_EQ("shi;uq", codes["\xe4\xb8\x96"]);  // 世
  EXPECT_EQ("jie;ub", codes["\xe7\x95\x8c"]);  // 界
  EXPECT_EQ("ni;re hao;nz", codes["\xe4\xbd\xa0\xe5\xa5\xbd"]);  // 你好
  EXPECT_EQ("zen;rx me;", codes["\xe6\x80\x8e\xe4\xb9\x88"]);    // 怎么
  // 你好，世界: punctuation is ignored, codes align by character
  EXPECT_EQ(
      "ni;re hao;nz shi;uq jie;ub",
      codes["\xe4\xbd\xa0\xe5\xa5\xbd\xef\xbc\x8c\xe4\xb8\x96\xe7\x95\x8c"]);
  // 好你: syllable already containing the separator is left untouched
  EXPECT_EQ("hao;nz ni;re", codes["\xe5\xa5\xbd\xe4\xbd\xa0"]);
  // 中: tone digit stripped before auxiliary code appended
  EXPECT_EQ("zhong;ll", codes["\xe4\xb8\xad"]);
  // 你中: tone digits stripped, then auxiliary codes appended
  EXPECT_EQ("ni;re zhong;ll", codes["\xe4\xbd\xa0\xe4\xb8\xad"]);
}

}  // namespace rime