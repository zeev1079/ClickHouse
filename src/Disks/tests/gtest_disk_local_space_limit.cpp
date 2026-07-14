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
    EXPECT_EQ(capped->getAvailableSpace(), uncapped->getAvailableSpace());
}

TEST_F(DiskLocalSpaceLimitTest, LargerThanRealDiskHasNoEffect)
{
    auto uncapped = makeDisk(path, 0);
    auto real_total = *uncapped->getTotalSpace();

    auto disk = makeDisk(path, real_total * 2);

    EXPECT_EQ(disk->getTotalSpace(), real_total);
    EXPECT_EQ(disk->getAvailableSpace(), uncapped->getAvailableSpace());
}

TEST_F(DiskLocalSpaceLimitTest, SmallerThanAvailableCapsAvailableSpace)
{
    auto uncapped = makeDisk(path, 0);
    auto real_total = *uncapped->getTotalSpace();
    auto real_used = real_total - *uncapped->getAvailableSpace();

    UInt64 max_disk_space_bytes = real_used + 1024 * 1024;
    ASSERT_LT(max_disk_space_bytes, real_total) << "test requires at least 1MiB of headroom on the tmp filesystem";

    auto disk = makeDisk(path, max_disk_space_bytes);

    EXPECT_EQ(disk->getTotalSpace(), max_disk_space_bytes);
    EXPECT_EQ(disk->getAvailableSpace(), max_disk_space_bytes - real_used);
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
