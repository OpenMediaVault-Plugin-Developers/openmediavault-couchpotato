<?php

/**
 * Copyright (C)      2013 OpenMediaVault Plugin Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

require_once "openmediavault/module.inc";
require_once "openmediavault/config.inc";
require_once "openmediavault/error.inc";
require_once "openmediavault/util.inc";
require_once "openmediavault/initscript.inc";

class OMVModuleCouchpotato extends OMVModuleServiceAbstract
    implements OMVINotifyListener, OMVIModuleServiceStatus {

    public function getName() {
        return "Couchpotato";
    }

    public function getStatus() {
        global $xmlConfig;

        $object = $xmlConfig->get("//services/couchpotato");

        if (is_null($object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, "//services/couchpotato");

        $cmd = "/etc/init.d/couchpotato status";
        $this->exec($cmd, $output);

        return array(
            "name"    => $this->getName(),
            "title"   => gettext("Couchpotato"),
            "enabled" => boolval($object['enable']),
            "running" => (strpos($output[0], 'is running') > 0)
        );
    }

    public function startService() {
        global $xmlConfig;
		$xpath  = "//services/couchpotato";
        $object = $xmlConfig->get($xpath);
        if (is_null($object))
			throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $xpath);

        if(FALSE === boolval($object['enable']))
            return;
		
		$cmd = "/etc/init.d/couchpotato status";
		$this->exec($cmd, $output);
		if (strpos($output[0], 'failed!') > 0);
			$cmd = "update-rc.d couchpotato enable && service couchpotato start 2>&1";
			$this->exec($cmd, $output);
    }

    public function stopService() {
        global $xmlConfig;
		$xpath  = "//services/couchpotato";
        $object = $xmlConfig->get($xpath);
        if (is_null($object))
			throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $xpath);

        if(TRUE === boolval($object['enable']))
            return;
		
		$cmd = "/etc/init.d/couchpotato status";
		$this->exec($cmd, $output);
		if (strpos($output[0], 'is running') > 0);
			$cmd = "service couchpotato stop && update-rc.d couchpotato disable 2>&1";
			$this->exec($cmd, $output);
    }

    public function bindListeners(OMVNotifyDispatcher $dispatcher) {
        $dispatcher->addListener(
            OMV_NOTIFY_MODIFY,
            "org.openmediavault.services.couchpotato",
            array($this, "setDirty"));
    }
}

// Register module
$moduleMgr = &OMVModuleMgr::getInstance();
$moduleMgr->registerModule(new OMVModuleCouchpotato());
