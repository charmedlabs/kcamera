/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * ipa_manager.h - Image Processing Algorithm module manager
 */
#ifndef __LIBCAMERA_INTERNAL_IPA_MANAGER_H__
#define __LIBCAMERA_INTERNAL_IPA_MANAGER_H__

#include <stdint.h>
#include <vector>

#include <libcamera/ipa/ipa_interface.h>
#include <libcamera/ipa/ipa_module_info.h>

#include "libcamera/internal/ipa_module.h"
#include "libcamera/internal/log.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/pub_key.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(IPAManager)

class IPAManager
{
public:
	IPAManager();
	~IPAManager();

	template<typename T>
	static std::unique_ptr<T> createIPA(PipelineHandler *pipe,
					    uint32_t maxVersion,
					    uint32_t minVersion)
	{
		IPAModule *m = nullptr;

		for (IPAModule *module : self_->modules_) {
			if (module->match(pipe, minVersion, maxVersion)) {
				m = module;
				break;
			}
		}

		if (!m)
			return nullptr;

		std::unique_ptr<T> proxy = std::make_unique<T>(m, !self_->isSignatureValid(m));
		if (!proxy->isValid()) {
			LOG(IPAManager, Error) << "Failed to load proxy";
			return nullptr;
		}

		return proxy;
	}

private:
	static IPAManager *self_;

	void parseDir(const char *libDir, unsigned int maxDepth,
		      std::vector<std::string> &files);
	unsigned int addDir(const char *libDir, unsigned int maxDepth = 0);

	bool isSignatureValid(IPAModule *ipa) const;

	std::vector<IPAModule *> modules_;

#if HAVE_IPA_PUBKEY
	static const uint8_t publicKeyData_[];
	static const PubKey pubKey_;
#endif
};

} /* namespace libcamera */

#endif /* __LIBCAMERA_INTERNAL_IPA_MANAGER_H__ */
