#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::dxbc {

void testDxbcTypeToIrType();
void testDxbcTypeFromIrType();
void testDxbcSwizzle();
void testDxbcSampleControlToken();
void testDxbcResourceDimToken();
void testDxbcResourceTypeToken();
void testDxbcOpCodeToken();
void testDxbcOperandToken();
void testDxbcSignatureParseIsgn();
void testDxbcSignatureParseOsgn();
void testDxbcSignatureParsePcsg();
void testDxbcSignatureParseOsg5();
void testDxbcSignatureParseIsg1();
void testDxbcSignatureParseOsg1();
void testDxbcSignatureParsePsg1();
void testDxbcSignatureSearch();
void testDxbcSignatureFilter();
void testDxbcSignatureEncodeOsgn();
void testDxbcSignatureEncodeOsg5();
void testDxbcSignatureEncodeOsg1();
void testDxbcHash();

void runTests() {
  RUN_TEST(testDxbcTypeToIrType);
  RUN_TEST(testDxbcTypeFromIrType);
  RUN_TEST(testDxbcSwizzle);
  RUN_TEST(testDxbcSampleControlToken);
  RUN_TEST(testDxbcResourceDimToken);
  RUN_TEST(testDxbcResourceTypeToken);
  RUN_TEST(testDxbcOpCodeToken);
  RUN_TEST(testDxbcOperandToken);
  RUN_TEST(testDxbcSignatureParseIsgn);
  RUN_TEST(testDxbcSignatureParseOsgn);
  RUN_TEST(testDxbcSignatureParsePcsg);
  RUN_TEST(testDxbcSignatureParseOsg5);
  RUN_TEST(testDxbcSignatureParseIsg1);
  RUN_TEST(testDxbcSignatureParseOsg1);
  RUN_TEST(testDxbcSignatureParsePsg1);
  RUN_TEST(testDxbcSignatureSearch);
  RUN_TEST(testDxbcSignatureFilter);
  RUN_TEST(testDxbcSignatureEncodeOsgn);
  RUN_TEST(testDxbcSignatureEncodeOsg5);
  RUN_TEST(testDxbcSignatureEncodeOsg1);
  RUN_TEST(testDxbcHash);
}

}
