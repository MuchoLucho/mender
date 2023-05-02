// Copyright 2023 Northern.tech AS
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#include <mender-update/update_module/v3/update_module.hpp>

#include <sys/stat.h>

#include <algorithm>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <common/common.hpp>
#include <common/path.hpp>
#include <common/key_value_database_lmdb.hpp>
#include <common/testing.hpp>
#include <common/processes.hpp>

#include <mender-update/context.hpp>
#include <string>
#include <sstream>

namespace io = mender::common::io;
namespace error = mender::common::error;
namespace expected = mender::common::expected;
namespace common = mender::common;
namespace conf = mender::common::conf;
namespace context = mender::update::context;
namespace update_module = mender::update::update_module::v3;
namespace path = mender::common::path;
namespace json = mender::common::json;


namespace processes = mender::common::processes;


using namespace std;
using namespace mender::common::testing;

class UpdateModuleTests : public testing::Test {
public:
	TemporaryDirectory temp_dir_;
	string test_scripts_dir_;
	string work_dir_;

	void SetUp() override {
		// mender::common::log::SetLevel(mender::common::log::LogLevel::Debug);

		test_scripts_dir_ = path::Join(temp_dir_.Path(), "modules");
		ASSERT_EQ(mkdir(test_scripts_dir_.c_str(), 0700), 0);
		test_scripts_dir_ = path::Join(test_scripts_dir_, "v3");
		ASSERT_EQ(mkdir(test_scripts_dir_.c_str(), 0700), 0);

		work_dir_ = path::Join(temp_dir_.Path(), "work");
		ASSERT_EQ(mkdir(work_dir_.c_str(), 0700), 0);
	}

	bool PrepareTestFile(const string &name, bool executable) {
		string test_file_path {path::Join(test_scripts_dir_, name)};
		ofstream os(test_file_path);
		os.close();

		if (executable) {
			int ret = chmod(test_file_path.c_str(), 0700);
			return ret == 0;
		}
		return true;
	}

	expected::ExpectedString PrepareUpdateModuleScript(update_module::UpdateModule &update_module) {
		string name = "update-module";
		if (!PrepareTestFile(name, true)) {
			int err = errno;
			return expected::unexpected(error::Error(
				generic_category().default_error_condition(err),
				"Cannot create update module script"));
		}

		string test_file_path {path::Join(test_scripts_dir_, name)};
		update_module.update_module_path_ = test_file_path;
		update_module.update_module_workdir_ = path::Join(temp_dir_.Path(), "work");
		return test_file_path;
	}

	expected::ExpectedString PrepareArtifact(size_t mb = 1, size_t number_of_files = 1) {
		auto rootfs = path::Join(temp_dir_.Path(), "rootfs");
		{
			processes::Process proc(
				{"dd", "if=/dev/urandom", "of=" + rootfs, "bs=1M", "count=" + to_string(mb)});
			auto err = proc.Run();
			if (err != error::NoError) {
				return expected::unexpected(err);
			}
		}

		auto file = path::Join(temp_dir_.Path(), "artifact.mender");
		vector<string> args {
			"mender-artifact",
			"write",
			"module-image",
			"-T",
			"rootfs-image-v2",
			"-o",
			file,
			"-n",
			"test",
			"-t",
			"test",
			"-f",
			rootfs};
		for (size_t index = 1; index < number_of_files; index++) {
			auto extra_rootfs = rootfs + to_string(index + 1);
			processes::Process proc({"cp", rootfs, extra_rootfs});
			auto err = proc.Run();
			if (err != error::NoError) {
				return expected::unexpected(err);
			}

			args.push_back("-f");
			args.push_back(extra_rootfs);
		}
		{
			processes::Process proc(args);
			auto err = proc.Run();
			if (err != error::NoError) {
				return expected::unexpected(err);
			}
		}

		{
			processes::Process proc({"mender-artifact", "read", file});
			auto err = proc.Run();
			if (err != error::NoError) {
				return expected::unexpected(err);
			}
		}
		return file;
	}
};

TEST_F(UpdateModuleTests, DiscoverUpdateModulesTest) {
	auto ok = PrepareTestFile("file1", false);
	ASSERT_TRUE(ok);

	ok = PrepareTestFile("script1", true);
	ASSERT_TRUE(ok);

	ok = PrepareTestFile("file2", false);
	ASSERT_TRUE(ok);

	ok = PrepareTestFile("script2", true);
	ASSERT_TRUE(ok);

	auto cfg = conf::MenderConfig();
	cfg.data_store_dir = temp_dir_.Path();

	auto ex_modules = update_module::DiscoverUpdateModules(cfg);
	ASSERT_TRUE(ex_modules);
	auto modules = ex_modules.value();
	EXPECT_EQ(modules.size(), 2);
	EXPECT_EQ(count(modules.cbegin(), modules.cend(), path::Join(test_scripts_dir_, "script1")), 1);
	EXPECT_EQ(count(modules.cbegin(), modules.cend(), path::Join(test_scripts_dir_, "script2")), 1);
}

TEST_F(UpdateModuleTests, DiscoverUpdateModulesNoExistTest) {
	auto cfg = conf::MenderConfig();
	cfg.data_store_dir = temp_dir_.Path();

	auto ex_modules = update_module::DiscoverUpdateModules(cfg);
	ASSERT_TRUE(ex_modules);

	auto modules = ex_modules.value();
	EXPECT_EQ(modules.size(), 0);
}

TEST_F(UpdateModuleTests, DiscoverUpdateModulesEmptyDirTest) {
	auto cfg = conf::MenderConfig();
	cfg.data_store_dir = temp_dir_.Path();

	auto ex_modules = update_module::DiscoverUpdateModules(cfg);
	ASSERT_TRUE(ex_modules);

	auto modules = ex_modules.value();
	EXPECT_EQ(modules.size(), 0);
}

TEST_F(UpdateModuleTests, DiscoverUpdateModulesNoExecutablesTest) {
	auto ok = PrepareTestFile("file1", false);
	ASSERT_TRUE(ok);

	ok = PrepareTestFile("file2", false);
	ASSERT_TRUE(ok);

	auto cfg = conf::MenderConfig();
	cfg.data_store_dir = temp_dir_.Path();

	auto ex_modules = update_module::DiscoverUpdateModules(cfg);
	ASSERT_TRUE(ex_modules);
	auto modules = ex_modules.value();
	EXPECT_EQ(modules.size(), 0);
}

class UpdateModuleFileTreeTests : public testing::Test {
public:
	void SetUp() override {
		this->cfg.data_store_dir = test_state_dir.Path();

		this->ctx = make_shared<context::MenderContext>(cfg);
		auto err = ctx->Initialize();
		ASSERT_EQ(err, error::NoError);

		auto &db = ctx->GetMenderStoreDB();
		err = db.Write(
			"artifact-name", common::ByteVectorFromString("artifact-name existing-artifact-name"));
		ASSERT_EQ(err, error::NoError);
		err = db.Write(
			"artifact-group",
			common::ByteVectorFromString("artifact-group existing-artifact-group"));
		ASSERT_EQ(err, error::NoError);

		ofstream os(path::Join(cfg.data_store_dir, "device_type"));
		ASSERT_TRUE(os);
		os << "device_type=Some device type" << endl;
		os.close();

		ASSERT_TRUE(CreateArtifact());
		std::fstream fs {path::Join(temp_dir.Path(), "artifact.mender")};
		io::StreamReader sr {fs};
		auto expected_artifact = mender::artifact::Parse(sr);
		ASSERT_TRUE(expected_artifact);
		auto artifact = make_shared<mender::artifact::Artifact>(expected_artifact.value());

		auto expected_payload_header = mender::artifact::View(*artifact, 0);
		ASSERT_TRUE(expected_payload_header) << expected_payload_header.error().message;

		this->update_payload_header = make_shared<mender::artifact::PayloadHeaderView>(
			mender::artifact::View(*artifact, 0).value());
	}

	bool CreateArtifact() {
		string script = R"(#! /bin/sh

DIRNAME=$(dirname $0)

# Create small tar file
echo foobar > ${DIRNAME}/testdata
mender-artifact \
    --compression none \
    write rootfs-image \
    --no-progress \
    -t test-device \
    -n test-artifact \
    -f ${DIRNAME}/testdata \
    -o ${DIRNAME}/artifact.mender || exit 1

exit 0
		)";

		const string script_fname = path::Join(temp_dir.Path(), "/test-script.sh");

		std::ofstream os(script_fname.c_str(), std::ios::out);
		os << script;
		os.close();

		int ret = chmod(script_fname.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
		if (ret != 0) {
			return ret;
		}

		processes::Process proc({script_fname});
		auto ex_line_data = proc.GenerateLineData();
		if (!ex_line_data) {
			return false;
		}
		EXPECT_EQ(proc.GetExitStatus(), 0) << "error message: " + ex_line_data.error().message;
		return true;
	}

protected:
	TemporaryDirectory test_state_dir;
	TemporaryDirectory test_tree_dir;
	TemporaryDirectory temp_dir;

	conf::MenderConfig cfg {};
	shared_ptr<context::MenderContext> ctx;
	shared_ptr<mender::artifact::Payload> payload;
	shared_ptr<mender::artifact::PayloadHeaderView> update_payload_header;
};

TEST_F(UpdateModuleFileTreeTests, FileTreeTestHeader) {
	update_module::UpdateModule up_mod(*ctx, *payload, *update_payload_header);
	const string tree_path = test_tree_dir.Path();
	auto err = up_mod.PrepareFileTree(tree_path);
	ASSERT_EQ(err, error::NoError);

	//
	// Current device contents
	//

	EXPECT_TRUE(FileContains(path::Join(tree_path, "version"), "3\n"));

	EXPECT_TRUE(FileContains(
		path::Join(tree_path, "current_artifact_name"), "artifact-name existing-artifact-name\n"));

	EXPECT_TRUE(FileContains(
		path::Join(tree_path, "current_artifact_group"),
		"artifact-group existing-artifact-group\n"));

	EXPECT_TRUE(FileContains(path::Join(tree_path, "current_device_type"), "Some device type\n"));

	//
	// Header contents (From the Artifact)
	//

	EXPECT_TRUE(FileContains(path::Join(tree_path, "header", "artifact_group"), ""));

	EXPECT_TRUE(FileContains(path::Join(tree_path, "header", "artifact_name"), "test-artifact"));

	EXPECT_TRUE(FileContains(path::Join(tree_path, "header", "payload_type"), "rootfs-image"));

	string expected_header_info = R"(
	{
	  "artifact_depends": {
	    "device_type": [
	      "test-device"
	    ]
	  },
	  "artifact_provides": {
	    "artifact_name": "test-artifact"
	  },
	  "payloads": [
	    {
	      "type": "rootfs-image"
	    }
	  ]
	}
	)";
	EXPECT_TRUE(
		FileJsonEquals(path::Join(tree_path, "header", "header_info"), expected_header_info));


	string expected_type_info = R"(
	{
	  "artifact_provides": {
	    "rootfs-image.checksum":
	    "aec070645fe53ee3b3763059376134f058cc337247c978add178b6ccdfb0019f",
	    "rootfs-image.version": "test-artifact"
	  },
	  "clears_artifact_provides": [
	    "artifact_group",
	    "rootfs_image_checksum",
	    "rootfs-image.*"
	  ],
	  "type": ""
	})";
	EXPECT_TRUE(FileJsonEquals(path::Join(tree_path, "header", "type_info"), expected_type_info));

	EXPECT_TRUE(FileContains(path::Join(tree_path, "header", "meta_data"), ""));

	err = up_mod.DeleteFileTree(tree_path);
	ASSERT_EQ(err, error::NoError);
}

class DefaultArtifact {
public:
	void CreateArtifact(UpdateModuleTests &tests, size_t mb = 1, size_t number_of_files = 1) {
		auto maybe_artifact = tests.PrepareArtifact(mb, number_of_files);
		ASSERT_TRUE(maybe_artifact) << maybe_artifact.error();
		auto artifact_file = maybe_artifact.value();

		is = make_unique<ifstream>(artifact_file);
		ASSERT_TRUE(is->good());
		artifact_reader = make_unique<io::StreamReader>(*is);

		ctx = make_unique<context::MenderContext>(config);

		auto maybe_parsed = mender::artifact::parser::Parse(*artifact_reader);
		ASSERT_TRUE(maybe_parsed) << maybe_parsed.error();
		artifact = make_unique<mender::artifact::Artifact>(maybe_parsed.value());

		auto maybe_payload = artifact->Next();
		ASSERT_TRUE(maybe_payload) << maybe_payload.error();
		payload = make_unique<mender::artifact::Payload>(maybe_payload.value());

		auto maybe_payload_meta_data = mender::artifact::View(*artifact, 0);
		ASSERT_TRUE(maybe_payload_meta_data) << maybe_payload_meta_data.error();
		payload_meta_data =
			make_unique<mender::artifact::PayloadHeaderView>(maybe_payload_meta_data.value());

		update_module =
			make_unique<update_module::UpdateModule>(*ctx, *payload, *payload_meta_data);
	}

	unique_ptr<ifstream> is;
	unique_ptr<io::StreamReader> artifact_reader;
	conf::MenderConfig config;
	unique_ptr<context::MenderContext> ctx;
	unique_ptr<mender::artifact::Artifact> artifact;
	unique_ptr<mender::artifact::Payload> payload;
	unique_ptr<mender::artifact::PayloadHeaderView> payload_meta_data;
	unique_ptr<update_module::UpdateModule> update_module;
};

TEST_F(UpdateModuleTests, DownloadProcessFailsImmediately) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"(#!/bin/bash
exit 2
)";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, processes::MakeError(processes::NonZeroExitStatusError, "").code);
	EXPECT_THAT(err.String(), testing::HasSubstr(" 2"));
}

TEST_F(UpdateModuleTests, DownloadProcess) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
cat "$file" > payload
file="$(cat stream-next)"
test "$file" = ""
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_EQ(err, error::NoError) << err.String();
	EXPECT_TRUE(
		FilesEqual(path::Join(work_dir_, "payload"), path::Join(temp_dir_.Path(), "rootfs")));
}

TEST_F(UpdateModuleTests, DownloadProcessDiesMidway) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
dd if="$file" of=payload bs=1048576 bs=123456 count=1
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, make_error_condition(errc::broken_pipe)) << err.String();
}

TEST_F(UpdateModuleTests, DownloadProcessDoesntOpenStream) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, make_error_condition(errc::broken_pipe)) << err.String();
}

TEST_F(UpdateModuleTests, DownloadProcessOpensStreamNextButDoesntRead) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
dd if=stream-next count=0
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, make_error_condition(errc::broken_pipe)) << err.String();
}

TEST_F(UpdateModuleTests, DownloadProcessCrashesAfterStreamNext) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
exit 2
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, processes::MakeError(processes::NonZeroExitStatusError, "").code)
		<< err.String();
}

TEST_F(UpdateModuleTests, DownloadProcessReadsEverythingExceptLastEntry) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
cat "$file" > payload
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, make_error_condition(errc::broken_pipe)) << err.String();
}

TEST_F(UpdateModuleTests, DownloadProcessTwoFiles) {
	DefaultArtifact art;
	art.CreateArtifact(*this, 1, 2);
	ASSERT_FALSE(HasFailure());

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"

file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
cat "$file" > payload1

file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs2"
cat "$file" > payload2

file="$(cat stream-next)"
test "$file" = ""
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_EQ(err, error::NoError) << err.String();
	EXPECT_TRUE(
		FilesEqual(path::Join(work_dir_, "payload1"), path::Join(temp_dir_.Path(), "rootfs")));
	EXPECT_TRUE(
		FilesEqual(path::Join(work_dir_, "payload2"), path::Join(temp_dir_.Path(), "rootfs2")));
}

TEST_F(UpdateModuleTests, DownloadProcessStoreFiles) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
exit 0
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_EQ(err, error::NoError) << err.String();
	EXPECT_TRUE(
		FilesEqual(path::Join(temp_dir_.Path(), "rootfs"), path::Join(work_dir_, "files/rootfs")));
}

TEST_F(UpdateModuleTests, DownloadProcessStoreTwoFiles) {
	DefaultArtifact art;
	art.CreateArtifact(*this, 1, 2);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
exit 0
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_EQ(err, error::NoError) << err.String();
	EXPECT_TRUE(
		FilesEqual(path::Join(temp_dir_.Path(), "rootfs"), path::Join(work_dir_, "files/rootfs")));
	EXPECT_TRUE(
		FilesEqual(path::Join(temp_dir_.Path(), "rootfs"), path::Join(work_dir_, "files/rootfs2")));
}

TEST_F(UpdateModuleTests, DownloadProcessStoreFilesFailure) {
	// Make sure we get a sensible failure if storing a file failed. Running out of space is
	// more likely than the error we make here (directory blocks the path), but we still test
	// the error path.

	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
mkdir -p files/rootfs
exit 0
)delim";
	}

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, make_error_condition(errc::is_a_directory)) << err.String();
}

TEST_F(UpdateModuleTests, DownloadProcessTimesOut) {
	DefaultArtifact art;
	art.CreateArtifact(*this);

	auto maybe_script = PrepareUpdateModuleScript(*art.update_module);
	ASSERT_TRUE(maybe_script) << maybe_script.error();
	auto script_path = maybe_script.value();
	{
		ofstream um_script(script_path);
		um_script << R"delim(#!/bin/bash
set -e
echo "Update Module called"
test "$1" = "Download"
file="$(cat stream-next)"
echo "Got file $file"
test "$file" = "streams/rootfs"
sleep 2
)delim";
	}

	// Set only 1 second timeout.
	art.config.module_timeout_seconds = 1;

	auto err = art.update_module->Download();
	EXPECT_NE(err, error::NoError) << err.String();
	EXPECT_EQ(err.code, make_error_condition(errc::timed_out)) << err.String();
}
