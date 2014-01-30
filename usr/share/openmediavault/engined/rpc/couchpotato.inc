<?php

/**
 * Copyright (C) 2013 OpenMediaVault Plugin Developers
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

require_once "openmediavault/object.inc";
require_once "openmediavault/config.inc";
require_once "openmediavault/error.inc";
require_once "openmediavault/util.inc";
require_once "openmediavault/rpcservice.inc";
require_once "openmediavault/notify.inc";

class OMVRpcServiceCouchpotato extends OMVRpcServiceAbstract {

    private function getXPath()
    {
        return "//services/couchpotato";
    }

    public function getName() {
        return "Couchpotato";
    }

    public function initialize() {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
    }

    function getSettings($params, $context) {
        global $xmlConfig;

        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR ));

        $object = $xmlConfig->get($this->getXPath());

        if(is_null($object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, "//services/couchpotato");

        $object["enable"] = boolval($object["enable"]);
        $object["showtab"] = boolval($object["showtab"]);
        $object["cp_branch"] = intval($object["cp_branch"]);

        return $object;
    }

    function setSettings($params, $context) {
        global $xmlConfig;

        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));

        $this->validateMethodParams($params, '{
            "type" : "object",
            "properties" : {
                "enable"        : { "type" : "boolean" },
                "showtab"       : { "type" : "boolean" },
                "cp_repo"       : { "type" : "string"},
                "cp_branch"     : { "type" : "integer","enum":[0,1]}
            }
        }');

        $oldObject = $xmlConfig->get($this->getXPath());

        if (is_null($oldObject))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $xpath);

        if ($params["cp_repo"] != "a" && $params["cp_branch"] == 1)
            throw new OMVException(OMVErrorMsg::E_MISC_FAILURE, "You CAN NOT use this branch with this repository.");

        if ($params["cp_repo"] != $oldObject["cp_repo"]) {
            $params["cp_branch"] = 0;
            $cmd = "omv-mkconf couchpotato swap_" . $params["cp_repo"];
        } elseif ($params["cp_branch"] != $oldObject["cp_branch"]) {
            if ($params["cp_branch"] == 0) {
                $cmd = "omv-mkconf couchpotato master 2>&1";
            }
            if ($params["cp_branch"] == 1) {
                $cmd = "omv-mkconf couchpotato develop 2>&1";
            }
        }

        $this->exec($cmd);
        $object = array(
            "enable"            => array_boolval($params, "enable"),
            "showtab"           => array_boolval($params, "showtab"),
            "cp_branch"         => $params["cp_branch"],
            "cp_repo"           => $params["cp_repo"]
        );

        if(false === $xmlConfig->replace($this->getXPath(), $object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED, "//services/couchpotato");

        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(OMV_NOTIFY_MODIFY, "org.openmediavault.services.couchpotato", $object);
    }
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceCouchpotato());

