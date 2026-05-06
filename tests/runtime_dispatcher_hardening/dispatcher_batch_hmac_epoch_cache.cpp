#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

#include <vmp/runtime/trampoline/trampoline.h>

#include "test_common.h"

int main() {
  using namespace vmp::runtime::trampoline;
  using namespace vmp::tests::runtime_dispatcher_hardening;

  const auto audit_path = temp_path("dispatcher_batch_hmac_epoch_cache", ".log");
  std::filesystem::remove(audit_path);

  const KeyContextId key_context{{
      0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60,
  }};

  ExecutableBuffer target_region(pattern(0x70, 64));

  TokenManager manager;
  const auto entry = manager.register_entry(key_context, 0x405000u, target_region.address(), "batch_gamma");
  StackFunctionTable table(manager.entries(), key_context, audit_path);

  std::vector<HmacBytes> derived_keys;
  std::vector<HmacBytes> scrubbed_keys;
  std::size_t nonce_index = 0;
  const std::uint64_t nonces[] = {
      0x1010101010101010ull,
      0x2020202020202020ull,
      0x3030303030303030ull,
  };

  DispatcherOptions options;
  options.batch_hmac_interval = 4;
  options.test_hooks.nonce_provider = [&]() {
    require(nonce_index < std::size(nonces), "nonce provider exhausted");
    return nonces[nonce_index++];
  };
  options.test_hooks.stack_canary_provider = []() { return static_cast<std::uintptr_t>(0x123456789abcdef0ull); };
  options.test_hooks.return_address_provider = []() { return static_cast<std::uintptr_t>(0x0fedcba987654321ull); };
  options.test_hooks.derived_key_observer = [&derived_keys](const HmacBytes& key) { derived_keys.push_back(key); };
  options.test_hooks.zeroized_key_observer = [&scrubbed_keys](const HmacBytes& key) { scrubbed_keys.push_back(key); };

  DispatcherResult first{};
  DispatcherResult second{};
  DispatcherResult third{};
  DispatcherResult fourth{};
  DispatcherResult fifth{};

  {
    Dispatcher dispatcher(table, options);
    first = dispatcher.dispatch_verbose(entry.token);
    second = dispatcher.dispatch_verbose(entry.token);
    third = dispatcher.dispatch_verbose(entry.token);
    fourth = dispatcher.dispatch_verbose(entry.token);

    require(first.integrity_ok && second.integrity_ok && third.integrity_ok && fourth.integrity_ok,
            "first epoch dispatches should succeed");
    require(first.stack_table_verified, "epoch head must perform full HMAC verification");
    require(!second.stack_table_verified && !third.stack_table_verified && !fourth.stack_table_verified,
            "non-head dispatches in the same epoch should skip full HMAC verification");
    require(first.hmac_epoch == 1u && fourth.hmac_epoch == 1u, "unexpected first epoch id");
    require(derived_keys.size() == 1u, "epoch-scoped HMAC key should derive once per epoch");
    require(scrubbed_keys.empty(), "cached HMAC key should stay resident until epoch rotation");

    fifth = dispatcher.dispatch_verbose(entry.token);
    require(fifth.integrity_ok, "second epoch head should still succeed");
    require(fifth.stack_table_verified, "epoch rollover must immediately re-verify the table");
    require(fifth.hmac_epoch == 2u, "unexpected second epoch id");
    require(derived_keys.size() == 2u, "second epoch should derive exactly one additional HMAC key");
    require(scrubbed_keys.size() == 1u, "previous epoch key should be scrubbed on rollover");
    require(std::all_of(scrubbed_keys.front().begin(), scrubbed_keys.front().end(),
                        [](std::uint8_t byte) { return byte == 0; }),
            "scrubbed rollover key must be zeroized");
  }

  require(scrubbed_keys.size() == 2u, "dispatcher destruction should scrub the final cached key");
  require(std::all_of(scrubbed_keys.back().begin(), scrubbed_keys.back().end(),
                      [](std::uint8_t byte) { return byte == 0; }),
          "final cached key must be zeroized on destruction");

  std::cout << "dispatcher_batch_hmac_epoch_cache OK\n";
  return 0;
}
