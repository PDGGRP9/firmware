#include <unity.h>
#include <string.h>

const char* getMessage();

void test_message_content(void) {
  TEST_ASSERT_EQUAL_STRING("Hello World!", getMessage());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_message_content);
  return UNITY_END();
}
