// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "avif/internal.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

//------------------------------------------------------------------------------

class AvifExpression : public avifSampleTransformExpression {
 public:
  AvifExpression() : avifSampleTransformExpression{} {}
  ~AvifExpression() { avifArrayDestroy(this); }

  void AddConstant(int32_t constant) {
    avifSampleTransformToken& token = AddToken();
    token.type = AVIF_SAMPLE_TRANSFORM_CONSTANT;
    token.constant = constant;
  }
  void AddImage(uint8_t inputImageItemIndex) {
    avifSampleTransformToken& token = AddToken();
    token.type = AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX;
    token.inputImageItemIndex = inputImageItemIndex;
  }
  void AddOperator(avifSampleTransformTokenType op) {
    avifSampleTransformToken& token = AddToken();
    token.type = op;
  }

  int32_t Apply() const {
    ImagePtr result(avifImageCreate(/*width=*/1, /*height=*/1, /*depth=*/8,
                                    AVIF_PIXEL_FORMAT_YUV444));
    if (result.get() == nullptr) abort();
    if (avifImageAllocatePlanes(result.get(), AVIF_PLANES_YUV) !=
        AVIF_RESULT_OK) {
      abort();
    }

    if (avifImageApplyExpression(result.get(),
                                 AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_32, this,
                                 /*numInputImageItems=*/0, nullptr,
                                 AVIF_PLANES_YUV) != AVIF_RESULT_OK) {
      abort();
    }
    return result->yuvPlanes[0][0];
  }

 private:
  avifSampleTransformToken& AddToken() {
    if (tokens == nullptr &&
        !avifArrayCreate(this, sizeof(avifSampleTransformToken), 1)) {
      abort();
    }
    avifSampleTransformToken* token =
        reinterpret_cast<avifSampleTransformToken*>(avifArrayPush(this));
    if (token == nullptr) abort();
    return *token;
  }
};

int32_t Eval(int32_t a, int32_t b, avifSampleTransformTokenType op) {
  AvifExpression e;
  e.AddConstant(a);
  e.AddConstant(b);
  e.AddOperator(op);
  return e.Apply();
}

//------------------------------------------------------------------------------

TEST(SampleTransformTest, NoExpression) {
  AvifExpression empty;
  ASSERT_EQ(
      avifSampleTransformRecipeToExpression(AVIF_SAMPLE_TRANSFORM_NONE, &empty),
      AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_TRUE(avifSampleTransformExpressionIsEquivalentTo(&empty, &empty));
}

TEST(SampleTransformTest, NoRecipe) {
  AvifExpression empty;
  avifSampleTransformRecipe recipe;
  ASSERT_EQ(avifSampleTransformExpressionToRecipe(&empty, &recipe),
            AVIF_RESULT_OK);
  EXPECT_EQ(recipe, AVIF_SAMPLE_TRANSFORM_NONE);
}

TEST(SampleTransformTest, RecipeToExpression) {
  for (avifSampleTransformRecipe recipe :
       {AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B,
        AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B,
        AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_8B_OVERLAP_4B}) {
    AvifExpression expression;
    ASSERT_EQ(avifSampleTransformRecipeToExpression(recipe, &expression),
              AVIF_RESULT_OK);
    avifSampleTransformRecipe result;
    ASSERT_EQ(avifSampleTransformExpressionToRecipe(&expression, &result),
              AVIF_RESULT_OK);
    EXPECT_EQ(recipe, result);

    EXPECT_FALSE(avifSampleTransformExpressionIsValid(
        &expression, /*numInputImageItems=*/1));
    EXPECT_TRUE(avifSampleTransformExpressionIsValid(&expression,
                                                     /*numInputImageItems=*/2));
    EXPECT_TRUE(avifSampleTransformExpressionIsValid(&expression,
                                                     /*numInputImageItems=*/3));

    EXPECT_TRUE(
        avifSampleTransformExpressionIsEquivalentTo(&expression, &expression));
  }
}

TEST(SampleTransformTest, NotEquivalent) {
  AvifExpression a;
  ASSERT_EQ(avifSampleTransformRecipeToExpression(
                AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B, &a),
            AVIF_RESULT_OK);

  AvifExpression b;
  ASSERT_EQ(avifSampleTransformRecipeToExpression(
                AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B, &b),
            AVIF_RESULT_OK);

  EXPECT_FALSE(avifSampleTransformExpressionIsEquivalentTo(&a, &b));
}

TEST(SampleTransformTest, MaxStackSize) {
  AvifExpression e;
  for (int i = 0; i < 128; ++i) e.AddConstant(42);
  for (int i = 0; i < 127; ++i) e.AddOperator(AVIF_SAMPLE_TRANSFORM_SUM);

  EXPECT_EQ(e.Apply(), 255);
}

TEST(SampleTransformTest, Pow) {
  // Results are clamped to uint8_t.

  EXPECT_EQ(Eval(-2, INT32_MIN, AVIF_SAMPLE_TRANSFORM_POW), 0);
  EXPECT_EQ(Eval(-2, -3, AVIF_SAMPLE_TRANSFORM_POW), 0);
  EXPECT_EQ(Eval(-2, -2, AVIF_SAMPLE_TRANSFORM_POW), 0);
  EXPECT_EQ(Eval(-2, -1, AVIF_SAMPLE_TRANSFORM_POW), 0);
  EXPECT_EQ(Eval(-2, 0, AVIF_SAMPLE_TRANSFORM_POW), 1);
  EXPECT_EQ(Eval(-2, 1, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -2 clamped
  EXPECT_EQ(Eval(-2, 2, AVIF_SAMPLE_TRANSFORM_POW), 4);
  EXPECT_EQ(Eval(-2, 3, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -8 clamped
  EXPECT_EQ(Eval(-2, INT32_MAX - 1, AVIF_SAMPLE_TRANSFORM_POW),
            255);  // INT32_MAX clamped
  EXPECT_EQ(Eval(-2, INT32_MAX, AVIF_SAMPLE_TRANSFORM_POW),
            0);  // INT32_MIN clamped

  EXPECT_EQ(Eval(-1, INT32_MIN, AVIF_SAMPLE_TRANSFORM_POW), 1);
  EXPECT_EQ(Eval(-1, -3, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -1 clamped
  EXPECT_EQ(Eval(-1, -2, AVIF_SAMPLE_TRANSFORM_POW), 1);
  EXPECT_EQ(Eval(-1, -1, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -1 clamped
  EXPECT_EQ(Eval(-1, 0, AVIF_SAMPLE_TRANSFORM_POW), 1);
  EXPECT_EQ(Eval(-1, 1, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -1 clamped
  EXPECT_EQ(Eval(-1, 2, AVIF_SAMPLE_TRANSFORM_POW), 1);
  EXPECT_EQ(Eval(-1, 3, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -1 clamped
  EXPECT_EQ(Eval(-1, INT32_MAX - 1, AVIF_SAMPLE_TRANSFORM_POW), 1);
  EXPECT_EQ(Eval(-1, INT32_MAX, AVIF_SAMPLE_TRANSFORM_POW), 0);  // -1 clamped

  for (int32_t v : {0, 1}) {
    EXPECT_EQ(Eval(v, INT32_MIN, AVIF_SAMPLE_TRANSFORM_POW), v);
    EXPECT_EQ(Eval(v, -2, AVIF_SAMPLE_TRANSFORM_POW), v);
    EXPECT_EQ(Eval(v, -1, AVIF_SAMPLE_TRANSFORM_POW), v);
    EXPECT_EQ(Eval(v, 0, AVIF_SAMPLE_TRANSFORM_POW), v);
    EXPECT_EQ(Eval(v, 1, AVIF_SAMPLE_TRANSFORM_POW), v);
    EXPECT_EQ(Eval(v, 2, AVIF_SAMPLE_TRANSFORM_POW), v);
    EXPECT_EQ(Eval(v, INT32_MAX, AVIF_SAMPLE_TRANSFORM_POW), v);
  }

  EXPECT_EQ(Eval(-(1 << 16), 3, AVIF_SAMPLE_TRANSFORM_POW),
            0);  // INT32_MIN clamped
  EXPECT_EQ(Eval((1 << 16), 3, AVIF_SAMPLE_TRANSFORM_POW),
            255);  // INT32_MAX clamped
}

//------------------------------------------------------------------------------

struct Op {
  Op(int32_t l, avifSampleTransformTokenType o, int32_t r, uint32_t e)
      : left(l), right(r), op(o), expected_result(e), single_operand(false) {}
  Op(avifSampleTransformTokenType o, int32_t l, uint32_t e)
      : left(l), right(0), op(o), expected_result(e), single_operand(true) {}

  int32_t left;
  int32_t right;
  avifSampleTransformTokenType op;
  uint32_t expected_result;
  bool single_operand;
};

class SampleTransformOperationTest : public testing::TestWithParam<Op> {};

TEST_P(SampleTransformOperationTest, Apply) {
  AvifExpression expression;
  // Postfix notation.
  expression.AddConstant(GetParam().left);
  if (!GetParam().single_operand) expression.AddConstant(GetParam().right);
  expression.AddOperator(GetParam().op);

  EXPECT_EQ(expression.Apply(), GetParam().expected_result);
}

INSTANTIATE_TEST_SUITE_P(
    Operations, SampleTransformOperationTest,
    testing::Values(Op(AVIF_SAMPLE_TRANSFORM_NEGATION, 1, 0),
                    Op(AVIF_SAMPLE_TRANSFORM_NEGATION, -1, 1),
                    Op(AVIF_SAMPLE_TRANSFORM_NEGATION, 0, 0),
                    Op(AVIF_SAMPLE_TRANSFORM_NEGATION, -256, 255),
                    Op(AVIF_SAMPLE_TRANSFORM_ABSOLUTE, 1, 1),
                    Op(AVIF_SAMPLE_TRANSFORM_ABSOLUTE, -1, 1),
                    Op(AVIF_SAMPLE_TRANSFORM_ABSOLUTE, 256, 255),
                    Op(AVIF_SAMPLE_TRANSFORM_ABSOLUTE, -256, 255),
                    Op(1, AVIF_SAMPLE_TRANSFORM_SUM, 1, 2),
                    Op(255, AVIF_SAMPLE_TRANSFORM_SUM, 255, 255),
                    Op(1, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 1, 0),
                    Op(255, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 255, 0),
                    Op(255, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 0, 255),
                    Op(0, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 255, 0),
                    Op(1, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, -1, 2),
                    Op(-1, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 1, 0),
                    Op(1, AVIF_SAMPLE_TRANSFORM_PRODUCT, 1, 1),
                    Op(2, AVIF_SAMPLE_TRANSFORM_PRODUCT, 3, 6),
                    Op(1, AVIF_SAMPLE_TRANSFORM_QUOTIENT, 1, 1),
                    Op(2, AVIF_SAMPLE_TRANSFORM_QUOTIENT, 3, 0),
                    Op(1, AVIF_SAMPLE_TRANSFORM_AND, 1, 1),
                    Op(1, AVIF_SAMPLE_TRANSFORM_AND, 2, 0),
                    Op(7, AVIF_SAMPLE_TRANSFORM_AND, 15, 7),
                    Op(1, AVIF_SAMPLE_TRANSFORM_OR, 1, 1),
                    Op(1, AVIF_SAMPLE_TRANSFORM_OR, 2, 3),
                    Op(1, AVIF_SAMPLE_TRANSFORM_XOR, 3, 2),
                    Op(AVIF_SAMPLE_TRANSFORM_NOT, 254, 0),
                    Op(AVIF_SAMPLE_TRANSFORM_NOT, -1, 0),
                    Op(AVIF_SAMPLE_TRANSFORM_BSR, 0, 0),
                    Op(AVIF_SAMPLE_TRANSFORM_BSR, -1, 0),
                    Op(AVIF_SAMPLE_TRANSFORM_BSR, 61, 5),
                    Op(AVIF_SAMPLE_TRANSFORM_BSR,
                       std::numeric_limits<int32_t>::max(), 30),
                    Op(2, AVIF_SAMPLE_TRANSFORM_POW, 4, 16),
                    Op(4, AVIF_SAMPLE_TRANSFORM_POW, 2, 16),
                    Op(123, AVIF_SAMPLE_TRANSFORM_POW, 123, 255),
                    Op(123, AVIF_SAMPLE_TRANSFORM_MIN, 124, 123),
                    Op(123, AVIF_SAMPLE_TRANSFORM_MAX, 124, 124)));

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
