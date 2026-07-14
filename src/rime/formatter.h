//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2013-07-02 GONG Chen <chen.sst@gmail.com>
//

#ifndef RIME_FORMATTER_H_
#define RIME_FORMATTER_H_

#include <rime/common.h>
#include <rime/component.h>
#include <rime/ticket.h>

namespace rime {

class Engine;

class Formatter : public Class<Formatter, const Ticket&> {
 public:
  Formatter(const Ticket& ticket)
      : engine_(ticket.engine), name_space_(ticket.name_space),
        klass_(ticket.klass) {}
  virtual ~Formatter() = default;

  virtual void Format(string* text) = 0;

  string name_space() const { return name_space_; }
  string klass() const { return klass_; }

 protected:
  Engine* engine_;
  string name_space_;
  string klass_;
};

}  // namespace rime

#endif  // RIME_FORMATTER_H_
