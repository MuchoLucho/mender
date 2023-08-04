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

#include <iostream>
#include <string>
#include <vector>

#include <common/conf.hpp>
#include <common/setup.hpp>

using namespace std;

namespace error = mender::common::error;

int main(int argc, char *argv[]) {
	mender::common::setup::GlobalSetup();

	mender::common::conf::MenderConfig config;
	if (argc > 1) {
		vector<string> args(argv + 1, argv + argc);
		auto success = config.ProcessCmdlineArgs(args.begin(), args.end());
		if (!success) {
			cerr << "Failed to process command line options: " + success.error().String() << endl;
			return 1;
		}
	} else {
		auto err = config.LoadDefaults();
		if (error::NoError != err) {
			cerr << "Failed to process command line options: " + err.String() << endl;
			return 1;
		}
	}

	return 0;
}