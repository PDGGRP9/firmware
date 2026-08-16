#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_sanity(void) {
  TEST_ASSERT_TRUE(true);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_sanity);
  return UNITY_END();
}
