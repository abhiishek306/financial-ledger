#include <cstdio>
#include <filesystem>

#include "ledger/wal_reader.hpp"
#include "ledger/wal_writer.hpp"
#include "test_util.hpp"

using namespace ledger;

namespace {

Transaction make_tx(TransactionId id, Amount amount) {
    Transaction tx{};
    tx.id = id;
    tx.idempotency_key = IdempotencyKey{.high = id, .low = id * 7};
    LEDGER_CHECK(tx.legs.push_back(EntryLeg{.account_id = 1, .direction = Direction::Debit, .amount = amount}));
    LEDGER_CHECK(tx.legs.push_back(EntryLeg{.account_id = 2, .direction = Direction::Credit, .amount = amount}));
    return tx;
}

void test_roundtrip_and_corruption_detection() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ledger_test_wal.bin";
    std::filesystem::remove(path);

    {
        wal::BinaryWalWriter writer;
        LEDGER_CHECK(writer.open(path.string(), wal::SyncMode::SyncEveryWrite).has_value());
        LEDGER_CHECK(writer.append(make_tx(1, 100)).has_value());
        LEDGER_CHECK(writer.append(make_tx(2, 250)).has_value());
    }

    std::uint64_t offset_after_first_record = 0;
    {
        wal::BinaryWalReader reader;
        LEDGER_CHECK(reader.open(path.string()).has_value());

        wal::ReadResult first = reader.read_next();
        LEDGER_CHECK(first.outcome == wal::ReadOutcome::Ok);
        LEDGER_CHECK(first.transaction.id == 1);
        LEDGER_CHECK(first.transaction.legs.span()[0].amount == 100);
        offset_after_first_record = reader.last_valid_offset();

        wal::ReadResult second = reader.read_next();
        LEDGER_CHECK(second.outcome == wal::ReadOutcome::Ok);
        LEDGER_CHECK(second.transaction.id == 2);

        wal::ReadResult eof = reader.read_next();
        LEDGER_CHECK(eof.outcome == wal::ReadOutcome::EndOfLog);
    }

    // Corrupt a byte inside the second record's payload and verify CRC catches it.
    {
        FILE* f = std::fopen(path.string().c_str(), "r+b");
        LEDGER_CHECK(f != nullptr);
        std::fseek(f, static_cast<long>(offset_after_first_record) + 20, SEEK_SET);
        int byte = std::fgetc(f);
        std::fseek(f, static_cast<long>(offset_after_first_record) + 20, SEEK_SET);
        std::fputc(byte ^ 0xFF, f);
        std::fclose(f);
    }

    {
        wal::BinaryWalReader reader;
        LEDGER_CHECK(reader.open(path.string()).has_value());

        wal::ReadResult first = reader.read_next();
        LEDGER_CHECK(first.outcome == wal::ReadOutcome::Ok);
        LEDGER_CHECK(reader.last_valid_offset() == offset_after_first_record);

        wal::ReadResult corrupt = reader.read_next();
        LEDGER_CHECK(corrupt.outcome == wal::ReadOutcome::Corrupt);
        // Cursor must not have advanced past the last valid boundary.
        LEDGER_CHECK(reader.last_valid_offset() == offset_after_first_record);
    }

    LEDGER_CHECK(wal::truncate_wal(path.string(), offset_after_first_record).has_value());
    LEDGER_CHECK(std::filesystem::file_size(path) == offset_after_first_record);

    std::filesystem::remove(path);
}

} // namespace

int main() {
    test_roundtrip_and_corruption_detection();
    std::puts("test_wal: all checks passed");
    return 0;
}
