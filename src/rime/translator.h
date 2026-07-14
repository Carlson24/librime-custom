//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-05-02 Wensong He <snowhws@gmail.com>
//

#ifndef RIME_TRANSLATOR_H_
#define RIME_TRANSLATOR_H_

#include <rime/common.h>
#include <rime/component.h>
#include <rime/ticket.h>

namespace rime {

class Context;
class Engine;
class Translation;
struct Segment;

class Translator : public Class<Translator, const Ticket&> {
 public:
  explicit Translator(const Ticket& ticket)
      : engine_(ticket.engine), name_space_(ticket.name_space),
        klass_(ticket.klass) {}
  virtual ~Translator() = default;

  virtual an<Translation> Query(const string& input,
                                const Segment& segment) = 0;

  string name_space() const { return name_space_; }
  string klass() const { return klass_; }

 protected:
  Engine* engine_;
  string name_space_;
  string klass_;
};

}  // namespace rime

#endif  // RIME_TRANSLATOR_H_
