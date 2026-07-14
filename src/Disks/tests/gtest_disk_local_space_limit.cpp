#include <gtest/gtest.h>
#include <Disks/DiskLocal.h>
#include <Poco/AutoPtr.h>
#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/Document.h>
#include <Poco/Util/XMLConfiguration.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{

std::shared_ptr<DB::DiskLocal> makeDisk(const std::string & path, UInt64 max_disk_space_bytes)
{
    Poco::XML::DOMParser dom_parser;
    Poco::AutoPtr<Poco::XML::Document> document = dom_parser.parseString("<clickhouse></clickhouse>");
    Poco::AutoPtr<Poco::Util::XMLConfiguration> config = new Poco::Util::XMLConfiguration(document);

    return std::make_shared<DB::DiskLocal>("test_disk", path, /* keep_free_space_bytes_ */ 0, max_disk_space_bytes, *config, "disk");
}

/// `getAvailableSpace` issues its own `statvfs` call, so two reads taken microseconds apart can
/// differ by a few filesystem blocks because of concurrent activity on the underlying filesystem.
/// Assertions that compare two live available-space values therefore use a tolerance rather than
/// exact equality. The tolerance is far below the gigabyte-scale difference a real capping
/// regression would produce, so it does not weaken the tests. `getTotalSpace` reads `f_blocks`,
/// which is stable, so those assertions remain exact.
constexpr UInt64 space_jitter_tolerance_bytes = 16 * 1024 * 1024;

}

class DiskLocalSpaceLimitTest : public testing::Test
{
public:
    void SetUp() override
    {
        path = "tmp/disk_local_space_limit/";
        fs::create_directories(path);
    }

    void TearDown() override { fs::remove_all(path); }

    std::string path;
};

TEST_F(DiskLocalSpaceLimitTest, UnsetMatchesUncappedDisk)
{
    auto capped = makeDisk(path, 0);
    auto uncapped = makeDisk(path, 0);

    EXPECT_EQ(capped->getTotalSpace(), uncapped->getTotalSpace());
    EXPECT_NEAR(
        static_cast<double>(*capped->getAvailableSpace()),
        static_cast<double>(*uncapped->getAvailableSpace()),
        static_cast<double>(space_jitter_tolerance_bytes));
}

TEST_F(DiskLocalSpaceLimitTest, LargerThanRealDiskHasNoEffect)
{
    auto uncapped = makeDisk(path, 0);
    auto real_total = *uncapped->getTotalSpace();

    auto disk = makeDisk(path, real_total * 2);

    EXPECT_EQ(disk->getTotalSpace(), real_total);
    EXPECT_NEAR(
        static_cast<double>(*disk->getAvailableSpace()),
        static_cast<double>(*uncapped->getAvailableSpace()),
        static_cast<double>(space_jitter_tolerance_bytes));
}

TEST_F(DiskLocalSpaceLimitTest, SmallerThanAvailableCapsAvailableSpace)
{
    auto uncapped = makeDisk(path, 0);
    auto real_total = *uncapped->getTotalSpace();
    auto real_used = real_total - *uncapped->getAvailableSpace();

    /// Headroom is kept comfortably above `space_jitter_tolerance_bytes` so the tolerance below
    /// absorbs statvfs jitter without masking a `min(available, limit)` regression (which would
    /// report ~`real_used` here, orders of magnitude larger) or a spurious floor to zero.
    UInt64 max_disk_space_bytes = real_used + 64 * 1024 * 1024;
    ASSERT_LT(max_disk_space_bytes, real_total) << "test requires at least 64MiB of headroom on the tmp filesystem";

    auto disk = makeDisk(path, max_disk_space_bytes);

    EXPECT_EQ(disk->getTotalSpace(), max_disk_space_bytes);
    EXPECT_NEAR(
        static_cast<double>(*disk->getAvailableSpace()),
        static_cast<double>(max_disk_space_bytes - real_used),
        static_cast<double>(space_jitter_tolerance_bytes));
}

TEST_F(DiskLocalSpaceLimitTest, SmallerThanUsedFloorsAtZero)
{
    auto uncapped = makeDisk(path, 0);
    auto real_total = *uncapped->getTotalSpace();
    auto real_used = real_total - *uncapped->getAvailableSpace();
    ASSERT_GT(real_used, 1u) << "test requires the tmp filesystem to already have some used space";

    auto disk = makeDisk(path, real_used / 2);

    EXPECT_EQ(disk->getAvailableSpace(), 0u);
}
