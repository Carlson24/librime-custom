//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-11-27 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>
#include <boost/algorithm/string.hpp>
#include <utf8.h>
#include <rime/algo/strings.h>
#include <rime/dict/dict_settings.h>
#include <rime/dict/entry_collector.h>
#include <rime/dict/preset_vocabulary.h>

namespace rime {

EntryCollector::EntryCollector() {}

EntryCollector::EntryCollector(Syllabary&& fixed_syllabary)
    : syllabary(std::move(fixed_syllabary)), build_syllabary(false) {}

EntryCollector::~EntryCollector() {}

void EntryCollector::Configure(DictSettings* settings) {
  if (settings->use_preset_vocabulary()) {
    LoadPresetVocabulary(settings);
  }

  if (settings->use_rule_based_encoder()) {
    encoder.reset(new TableEncoder(this));
  } else {
    encoder.reset(new ScriptEncoder(this));
  }
  encoder->LoadSettings(settings);
}

void EntryCollector::Collect(const vector<path>& dict_files) {
  for (const path& dict_file : dict_files) {
    Collect(dict_file);
  }
  Finish();
}

void EntryCollector::LoadPresetVocabulary(DictSettings* settings) {
  auto vocabulary = settings->vocabulary();
  LOG(INFO) << "loading preset vocabulary: " << vocabulary;
  preset_vocabulary.reset(new PresetVocabulary(vocabulary));
  if (preset_vocabulary) {
    if (settings->max_phrase_length() > 0)
      preset_vocabulary->set_max_phrase_length(settings->max_phrase_length());
    if (settings->min_phrase_weight() > 0)
      preset_vocabulary->set_min_phrase_weight(settings->min_phrase_weight());
  }
}

bool EntryCollector::LoadAuxiliaryCodes(const path& file) {
  auxiliary_codes_.clear();
  auxiliary_enabled_ = false;
  std::ifstream fin(file.c_str());
  if (!fin) {
    LOG(ERROR) << "failed to load auxiliary code file: " << file;
    return false;
  }
  string line;
  int line_number = 0;
  while (getline(fin, line)) {
    ++line_number;
    boost::algorithm::trim_right(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto row = strings::split(line, "\t");
    if (row.size() < 2 || row[0].empty()) {
      LOG(WARNING) << "invalid auxiliary code at line " << line_number
                   << " in file: " << file;
      continue;
    }
    auxiliary_codes_[row[0]] = row[1];
  }
  fin.close();
  auxiliary_enabled_ = true;
  LOG(INFO) << "loaded " << auxiliary_codes_.size() << " auxiliary codes from "
            << file;
  return true;
}

bool EntryCollector::IsIgnoredAuxChar(const string& ch) const {
  // whitespace is always ignored when aligning auxiliary codes
  if (ch.size() == 1 && isspace(static_cast<unsigned char>(ch[0])))
    return true;
  return auxiliary_ignore_chars_.find(ch) != string::npos;
}

void EntryCollector::Collect(const path& dict_file) {
  LOG(INFO) << "collecting entries from " << dict_file;
  current_dict_file = dict_file.to_utf8_string();
  line_number = 0;
  // read table
  std::ifstream fin(dict_file.c_str());
  DictSettings settings;
  if (!settings.LoadDictHeader(fin)) {
    LOG(ERROR) << "missing dict settings.";
    return;
  }
  // column definitions
  int text_column = settings.GetColumnIndex("text");
  int code_column = settings.GetColumnIndex("code");
  int weight_column = settings.GetColumnIndex("weight");
  int stem_column = settings.GetColumnIndex("stem");
  if (text_column == -1) {
    LOG(ERROR) << "missing text column definition in file: " << dict_file
               << ".";
    return;
  }
  bool enable_comment = true;
  string line;
  while (getline(fin, line)) {
    boost::algorithm::trim_right(line);
    line_number++;
    // skip empty lines and comments
    if (line.empty())
      continue;
    if (enable_comment && line[0] == '#') {
      if (line == "# no comment") {
        // a "# no comment" line disables further comments
        enable_comment = false;
      }
      continue;
    }
    // read a dict entry
    auto row = strings::split(line, "\t");
    int num_columns = static_cast<int>(row.size());
    if (num_columns <= text_column || row[text_column].empty()) {
      LOG(WARNING) << "Missing entry text at #" << num_entries
                   << ", line: " << line_number
                   << " of file: " << current_dict_file << ".";
      continue;
    }
    const auto& word(row[text_column]);
    string code_str;
    string weight_str;
    string stem_str;
    if (code_column != -1 && num_columns > code_column &&
        !row[code_column].empty())
      code_str = row[code_column];
    if (weight_column != -1 && num_columns > weight_column &&
        !row[weight_column].empty())
      weight_str = row[weight_column];
    if (stem_column != -1 && num_columns > stem_column &&
        !row[stem_column].empty())
      stem_str = row[stem_column];
    // collect entry
    collection.insert(word);
    if (!code_str.empty()) {
      if (!enable_tone_) {
        // strip trailing tone digits (0-9) from each syllable
        RawCode raw_code;
        raw_code.FromString(code_str);
        for (auto& syllable : raw_code) {
          if (!syllable.empty() && syllable.back() >= '0' &&
              syllable.back() <= '9') {
            syllable.pop_back();
          }
        }
        code_str = raw_code.ToString();
      }
      if (auxiliary_enabled_) {
        RawCode raw_code;
        raw_code.FromString(code_str);
        size_t index = 0;
        const char* char_ptr = word.c_str();
        const char* char_end = char_ptr;
        while (*char_end != '\0' && index < raw_code.size()) {
          utf8::unchecked::next(char_end);
          string character(char_ptr, char_end - char_ptr);
          char_ptr = char_end;
          if (IsIgnoredAuxChar(character)) {
            continue;
          }
          if (raw_code[index].find(auxiliary_code_separator_) != string::npos) {
            ++index;
            continue;
          }
          auto aux = auxiliary_codes_.find(character);
          string aux_code =
              (aux != auxiliary_codes_.end()) ? aux->second : string();
          raw_code[index] += auxiliary_code_separator_ + aux_code;
          ++index;
        }
        code_str = raw_code.ToString();
      }
      CreateEntry(word, code_str, weight_str);
    } else {
      encode_queue.push({word, weight_str});
    }
    if (!stem_str.empty() && !code_str.empty()) {
      DLOG(INFO) << "add stem '" << word << "': "
                 << "[" << code_str << "] = [" << stem_str << "]";
      stems[word].insert(stem_str);
    }
  }
  fin.close();
  LOG(INFO) << "Pass 1: total " << num_entries << " entries collected.";
  LOG(INFO) << "num unique syllables: " << syllabary.size();
  LOG(INFO) << "num of entries to encode: " << encode_queue.size();
}

void EntryCollector::Finish() {
  while (!encode_queue.empty()) {
    const auto& phrase(encode_queue.front().first);
    const auto& weight_str(encode_queue.front().second);
    if (!encoder->EncodePhrase(phrase, weight_str)) {
      LOG(ERROR) << "Encode failure: '" << phrase << "'.";
    }
    encode_queue.pop();
  }
  LOG(INFO) << "Pass 2: total " << num_entries << " entries collected.";
  if (preset_vocabulary) {
    preset_vocabulary->Reset();
    string phrase, weight_str;
    while (preset_vocabulary->GetNextEntry(&phrase, &weight_str)) {
      if (collection.find(phrase) != collection.end())
        continue;
      if (!encoder->EncodePhrase(phrase, weight_str)) {
        LOG(WARNING) << "Encode failure: '" << phrase << "'.";
      }
    }
  }
  decltype(collection)().swap(collection);
  decltype(words)().swap(words);
  decltype(total_weight)().swap(total_weight);
  LOG(INFO) << "Pass 3: total " << num_entries << " entries collected.";
}

void EntryCollector::CreateEntry(const string& word,
                                 const string& code_str,
                                 const string& weight_str) {
  an<RawDictEntry> e = New<RawDictEntry>();
  e->raw_code.FromString(code_str);
  e->text = word;
  e->weight = 0.0;
  bool scaled = boost::ends_with(weight_str, "%");
  if ((weight_str.empty() || scaled) && preset_vocabulary) {
    preset_vocabulary->GetWeightForEntry(e->text, &e->weight);
  }
  if (scaled) {
    double percentage = 100.0;
    try {
      percentage = std::stod(weight_str.substr(0, weight_str.length() - 1));
    } catch (...) {
      LOG(WARNING) << "invalid entry definition at #" << num_entries
                   << ", line: " << line_number
                   << " of file: " << current_dict_file << ".";
      percentage = 100.0;
    }
    e->weight *= percentage / 100.0;
  } else if (!weight_str.empty()) {  // absolute weight
    try {
      e->weight = std::stod(weight_str);
    } catch (...) {
      LOG(WARNING) << "invalid entry definition at #" << num_entries
                   << ", line: " << line_number
                   << " of file: " << current_dict_file << ".";
      e->weight = 0.0;
    }
  }
  // learn new syllables, or check if syllables are in the fixed syllabary.
  for (const string& s : e->raw_code) {
    if (syllabary.find(s) == syllabary.end()) {
      if (build_syllabary) {
        syllabary.insert(s);
      } else {
        LOG(ERROR) << "dropping entry '" << e->text
                   << "' with invalid syllable: " << s;
        return;
      }
    }
  }
  // learn new word
  bool is_word = (e->raw_code.size() == 1);
  if (is_word) {
    auto& weights = words[e->text];
    if (std::find_if(weights.begin(), weights.end(), [&](const auto& p) {
          return p.first == code_str;
        }) != weights.end()) {
      LOG(WARNING) << "duplicate word definition '" << e->text << "': ["
                   << code_str << "].";
      return;
    }
    weights.push_back(std::make_pair(code_str, e->weight));
    total_weight[e->text] += e->weight;
  }
  entries.emplace_back(std::move(e));
  ++num_entries;
}

bool EntryCollector::TranslateWord(const string& word, vector<string>* result) {
  const auto& s = stems.find(word);
  if (s != stems.end()) {
    for (const string& stem : s->second) {
      result->push_back(stem);
    }
    return true;
  }
  const auto& w = words.find(word);
  if (w != words.end()) {
    std::sort(w->second.begin(), w->second.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& v : w->second) {
      const double kMinimalWeight = 0.05;  // 5%
      double min_weight = total_weight[word] * kMinimalWeight;
      if (v.second < min_weight)
        continue;
      result->push_back(v.first);
    }
    return true;
  }
  return false;
}

void EntryCollector::Dump(const path& file_path) const {
  std::ofstream out(file_path.c_str());
  out << "# syllabary:" << std::endl;
  for (const string& syllable : syllabary) {
    out << "# - " << syllable << std::endl;
  }
  out << std::endl;
  for (const auto& e : entries) {
    out << e->text << '\t' << e->raw_code.ToString() << '\t' << e->weight
        << std::endl;
  }
  out.close();
}

}  // namespace rime
