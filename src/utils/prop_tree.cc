// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "prop_tree.h"

#include <stdlib.h>

#include <iostream>
#include <limits>
#include <sstream>

namespace tera {

// these chars are not belong to symbols
bool IsIdentifierChar(const char c) {
    return ((c <= '9' && c >= '0') ||
            (c <= 'z' && c >= 'a') ||
            (c <= 'Z' && c >= 'A') ||
            c == '.' ||
            c == '_' ||
            c == '-');
}

Tokenizer::Tokenizer(const std::string& input)
    : origin_(input), cur_pos_(0) {}

Tokenizer::~Tokenizer() {}

void Tokenizer::ConsumeUselessChars() {
    while (cur_pos_ < origin_.size()) {
        switch (origin_[cur_pos_]) {
        case '\n':
        case '\t':
        case '\v':
        case ' ':
            cur_pos_++;
            continue;
        case '#':
            // reach a line-comment, discard all chars in this line
            while (++cur_pos_ < origin_.size()) {
                if (origin_[cur_pos_] == '\n') {
                    break;
                }
            }
            continue;
        default:
            return;
        }
    }
}

void Tokenizer::ConsumeIdentifier() {
    current_.clear();
    current_.type = IDENTIFIER;
    while (cur_pos_ < origin_.size()) {
        char c = origin_[cur_pos_];
        if (IsIdentifierChar(c)) {
            current_.push_back(c);
            cur_pos_++;
            continue;
        } else {
            break;
        }
    }
}

void Tokenizer::ConsumeSymbol() {
    current_.clear();
    current_.type = SYMBOL;
    current_.push_back(origin_[cur_pos_]);
    cur_pos_++;
}

bool Tokenizer::Next() {
    ConsumeUselessChars();
    if (cur_pos_ >= origin_.size()) {
        return false;
    }
    if (IsIdentifierChar(origin_[cur_pos_])) {
        ConsumeIdentifier();
    } else {
        ConsumeSymbol();
    }
    return true;
}

PropTree::PropTree()
    : root_(NULL),
      max_depth_(0),
      min_depth_(std::numeric_limits<int>::max()) {
}

PropTree::~PropTree() {
    delete root_;
}

void PropTree::Reset() {
    delete root_;
    max_depth_ = 0;
    min_depth_ = std::numeric_limits<int>::max();
    state_.clear();
}

bool PropTree::ParseFromString(const std::string& input) {
    Reset();
    int angle_braket_diff = 0;
    int brace_diff = 0;
    Tokenizer tr(input);
    std::deque<Tokenizer::Token> tokens;
    while (tr.Next()) {
        const Tokenizer::Token& token = tr.current();
        if (token.type == Tokenizer::SYMBOL) {
            if (token.text == "<") {
                angle_braket_diff++;
            } else if (token.text == ">") {
                if (angle_braket_diff <= 0) {
                    AddError("syntax error: \">\" should be after \"<\".");
                }
                angle_braket_diff--;
            } else if (token.text == "{") {
                brace_diff++;
            } else if (token.text == "}") {
                if (brace_diff <= 0) {
                    AddError("syntax error: \"}\" should be after \"{\".");
                }
                brace_diff--;
            }
        }
        tokens.push_back(token);
    }
    if (angle_braket_diff != 0) {
        AddError("syntax error: \"<\" and \">\" are not matching.");
        return false;
    }
    if (brace_diff != 0) {
        AddError("syntax error: \"{\" and \"}\" are not matching.");
        return false;
    }

    return ParseFromTokens(tokens, 1, &root_);
}

bool PropTree::ParseFromTokens(std::deque<Tokenizer::Token>& tokens,
                               int depth, Node** node) {
    if (tokens.size() == 0) {
        return true;
    }
    if (tokens.front().type != Tokenizer::IDENTIFIER) {
        AddError("syntax error: node name error: " + tokens.front().text);
        return false;
    }
    *node = new Node();
    Node*& node_t = *node;

    // get node name and pop it out from token queue
    node_t->name_ = tokens.front().text;
    tokens.pop_front();

    // get all props and pop them out from token queue
    if (tokens.size() > 0 && tokens.front().text == "<") {
        tokens.pop_front(); // pop "<"
        while (tokens.size() > 3 && tokens.front().text != ">") {
            if (tokens.front().text == ",") {
                // reach a comma, discard it
                tokens.pop_front();
            }
            std::string prop_name = tokens.front().text;
            tokens.pop_front();
            std::string eq = tokens.front().text;
            tokens.pop_front();
            std::string prop_value = tokens.front().text;
            tokens.pop_front();
            if (eq != "=" || prop_name == ">" || prop_value == ">") {
                AddError("syntax error: property format error: "
                         + prop_name + eq + prop_value);
                return false;
            }
            node_t->properties_[prop_name] = prop_value;
        }
        if (tokens.front().text != ">") {
            AddError("syntax error: property format error: " + tokens.front().text);
            return false;
        }
        tokens.pop_front();  // pop ">"
    } else {
        // none property
    }

    // get all children from token queue
    if (tokens.size() > 2) {
        if (tokens.front().text != "{" || tokens.back().text != "}") {
            AddError("syntax error: child node format error: " + node_t->name_
                     + tokens.front().text + " " + tokens.back().text);
            return false;
        }
        tokens.pop_front();  // pop "{"
        tokens.pop_back();  // pop "}"
        if (tokens.back().text == ",") {
            // discard the last ","
            tokens.pop_back();
        }

        std::deque<Tokenizer::Token> child_tokens;
        int is_inner_comma = 0;
        while (tokens.size() > 0) {
            child_tokens.push_back(tokens.front());
            std::string tokentext = tokens.front().text;
            tokens.pop_front();

            if (tokentext == "<" || tokentext == "{") {
                is_inner_comma++;
            } else if (tokentext == ">" || tokentext == "}") {
                is_inner_comma--;
            } else if (tokentext == "," && is_inner_comma == 0) {
                child_tokens.pop_back();  // pop the last ","
                node_t->children_.push_back(new Node);
                node_t->children_.back()->mother_ = node_t;
                if (!ParseFromTokens(child_tokens, depth + 1,
                                     &node_t->children_.back())) {
                    return false;
                }
                child_tokens.clear();
            }
        }
        // parse the last child
        node_t->children_.push_back(new Node);
        node_t->children_.back()->mother_ = node_t;
        if (!ParseFromTokens(child_tokens, depth + 1,
                             &node_t->children_.back())) {
            return false;
        }
    } else if (tokens.size() == 0 || tokens.size() == 2 ||
               (tokens.size() == 1 && tokens.front().text == ",")) {
        // none child
    } else {
        // never reach here
        abort();
    }
    if (node_t->children_.size() == 0) {
        // this is a leaf node
        if (depth > max_depth_) {
            max_depth_ = depth;
        }
        if (depth < min_depth_) {
            min_depth_ = depth;
        }
    }
    return true;
}

std::string PropTree::FormatString() {
    std::stringstream ss;
    ss << "\n";
    FormatNode("  ", root_, &ss);
    return ss.str();
}

void PropTree::FormatNode(const std::string& line_prefix, Node* node,
                          std::stringstream* ss) {
    *ss << line_prefix << node->name_;
    size_t prop_num = node->properties_.size();
    if (prop_num > 0) {
        size_t prop_cnt = 0;
        *ss << "<";
        std::map<std::string, std::string>::iterator it = node->properties_.begin();
        for (;it != node->properties_.end(); ++it) {
            *ss << it->first << "=" << it->second;
            if (++prop_cnt < prop_num) {
                *ss << ", ";
            }
        }
        *ss << ">";
    }
    size_t children_num = node->children_.size();
    if (children_num > 0) {
        std::string prefix = line_prefix + "    ";
        *ss << " {\n";
        for (size_t i = 0; i < children_num; ++i) {
            FormatNode(prefix, node->children_[i], ss);
            if (i < children_num - 1) {
                *ss << ",";
            }
            *ss << "\n";
        }
        *ss << line_prefix << "}";
    }
}

void PropTree::AddError(const std::string& error_str) {
    state_.append(error_str + "\n");
}
}  // namespace tera
