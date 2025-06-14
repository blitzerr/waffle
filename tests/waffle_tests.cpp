#include <catch2/catch_all.hpp> // For Catch2 v3.x

#include "waffle/waffle_core_detail.hpp"

TEST_CASE("Waffle::detail::parse_args_impl", "[waffle][detail][parser]") {
  using Waffle::Attribute; // For dummy arguments
  using Waffle::CausedBy;
  using Waffle::Id;
  using Waffle::kInvalidId;
  using Waffle::detail::parse_args_impl;
  using Waffle::detail::ParsedArgs;

  const Id id1{123};
  const Id id2{456};
  const Attribute dummy_attr1{}; // Default constructed attribute
  const Attribute dummy_attr2{}; // Default constructed attribute

  SECTION("No arguments") {
    ParsedArgs result = parse_args_impl();
    REQUIRE(result.cause == kInvalidId);
  }

  SECTION("Only CausedBy argument") {
    ParsedArgs result = parse_args_impl(CausedBy{id1});
    REQUIRE(result.cause == id1);
  }

  SECTION("CausedBy as the first argument, followed by other types") {
    ParsedArgs result =
        parse_args_impl(CausedBy{id1}, dummy_attr1, dummy_attr2);
    REQUIRE(result.cause == id1);
  }

  SECTION("CausedBy as a middle argument") {
    ParsedArgs result =
        parse_args_impl(dummy_attr1, CausedBy{id1}, dummy_attr2);
    REQUIRE(result.cause == id1);
  }

  SECTION("CausedBy as the last argument") {
    ParsedArgs result =
        parse_args_impl(dummy_attr1, dummy_attr2, CausedBy{id1});
    REQUIRE(result.cause == id1);
  }

  SECTION("No CausedBy argument, only other types") {
    ParsedArgs result = parse_args_impl(dummy_attr1, dummy_attr2);
    REQUIRE(result.cause == kInvalidId);
  }

  SECTION("Multiple CausedBy arguments (first one should be picked)") {
    ParsedArgs result_first =
        parse_args_impl(CausedBy{id1}, dummy_attr1, CausedBy{id2});
    REQUIRE(result_first.cause == id1);

    ParsedArgs result_middle =
        parse_args_impl(dummy_attr1, CausedBy{id1}, CausedBy{id2});
    REQUIRE(result_middle.cause == id1);
  }

  SECTION("CausedBy with const reference") {
    const CausedBy const_caused_by{id1};
    ParsedArgs result = parse_args_impl(const_caused_by, dummy_attr1);
    REQUIRE(result.cause == id1);
  }

  SECTION("With other non-Attribute, non-CausedBy types (should still find "
          "CausedBy or return kInvalidId)") {
    struct SomeOtherType {
      int value;
    };
    const SomeOtherType other_type_val{10};

    ParsedArgs result_with_other_found =
        parse_args_impl(other_type_val, CausedBy{id1}, dummy_attr1);
    REQUIRE(result_with_other_found.cause == id1);

    ParsedArgs result_with_other_not_found =
        parse_args_impl(other_type_val, dummy_attr1);
    REQUIRE(result_with_other_not_found.cause == kInvalidId);
  }
}
