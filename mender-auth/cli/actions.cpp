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

#include <mender-auth/cli/actions.hpp>

#include <string>
#include <memory>

#include <mender-auth/context.hpp>
#include <mender-auth/cli/keystore.hpp>

#include <common/conf.hpp>
#include <common/events.hpp>
#include <common/log.hpp>

#include <mender-auth/ipc/server.hpp>

namespace mender {
namespace auth {
namespace cli {

using namespace std;

namespace events = mender::common::events;
namespace log = mender::common::log;

namespace ipc = mender::auth::ipc;

shared_ptr<MenderKeyStore> KeystoreFromConfig(
	const conf::MenderConfig &config, const string &passphrase) {
	cli::StaticKey static_key = cli::StaticKey::No;
	string pem_file;
	string ssl_engine;

	// TODO: review and simplify logic as part of MEN-6668. See discussion at:
	// https://github.com/mendersoftware/mender/pull/1378#discussion_r1317185066
	if (config.https_client.key != "") {
		pem_file = config.https_client.key;
		ssl_engine = config.https_client.ssl_engine;
		static_key = cli::StaticKey::Yes;
	}
	if (config.security.auth_private_key != "") {
		pem_file = config.security.auth_private_key;
		ssl_engine = config.security.ssl_engine;
		static_key = cli::StaticKey::Yes;
	}
	if (config.https_client.key == "" && config.security.auth_private_key == "") {
		pem_file = config.paths.GetKeyFile();
		ssl_engine = config.https_client.ssl_engine;
		static_key = cli::StaticKey::No;
	}

	return make_shared<MenderKeyStore>(pem_file, ssl_engine, static_key, passphrase);
}

error::Error DoBootstrap(shared_ptr<MenderKeyStore> keystore, const bool force) {
	auto err = keystore->Load();
	if (err != error::NoError && err.code != MakeError(NoKeysError, "").code) {
		return err;
	}
	if (err.code == MakeError(NoKeysError, "").code || force) {
		log::Info("Generating new RSA key");
		err = keystore->Generate();
		if (err != error::NoError) {
			return err;
		}
		err = keystore->Save();
		if (err != error::NoError) {
			return err;
		}
	}
	return error::NoError;
}

DaemonAction::DaemonAction(shared_ptr<MenderKeyStore> keystore, const bool force_bootstrap) :
	keystore_(keystore),
	force_bootstrap_(force_bootstrap) {
}

ExpectedActionPtr DaemonAction::Create(
	const conf::MenderConfig &config, const string &passphrase, const bool force_bootstrap) {
	auto key_store = KeystoreFromConfig(config, passphrase);

	return make_shared<DaemonAction>(key_store, force_bootstrap);
}

error::Error DaemonAction::Execute(context::MenderContext &main_context) {
	auto err = DoBootstrap(keystore_, force_bootstrap_);
	if (err != error::NoError) {
		log::Error("Failed to bootstrap: " + err.String());
		return error::MakeError(error::ExitWithFailureError, "");
	}

	events::EventLoop loop {};

	ipc::Server ipc_server {loop, main_context.GetConfig()};

	err = ipc_server.Listen();
	if (err != error::NoError) {
		log::Error("Failed to start the listen loop");
		log::Error(err.String());
		return error::MakeError(error::ExitWithFailureError, "");
	}

	loop.Run();

	return error::NoError;
}

BootstrapAction::BootstrapAction(shared_ptr<MenderKeyStore> keystore, bool force_bootstrap) :
	keystore_(keystore),
	force_bootstrap_(force_bootstrap) {
}

ExpectedActionPtr BootstrapAction::Create(
	const conf::MenderConfig &config, const string &passphrase, bool force_bootstrap) {
	auto key_store = KeystoreFromConfig(config, passphrase);

	return make_shared<BootstrapAction>(key_store, force_bootstrap);
}

error::Error BootstrapAction::Execute(context::MenderContext &main_context) {
	return DoBootstrap(keystore_, force_bootstrap_);
}

} // namespace cli
} // namespace auth
} // namespace mender