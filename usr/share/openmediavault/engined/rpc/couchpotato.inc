<?php

/**
 * Copyright (C) 2013-2015 OpenMediaVault Plugin Developers
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

require_once "openmediavault/config.inc";
require_once "openmediavault/error.inc";
require_once "openmediavault/notify.inc";
require_once "openmediavault/object.inc";
require_once "openmediavault/rpcservice.inc";

class OMVRpcServiceCouchpotato extends OMVRpcServiceAbstract
{
    private $couchpotatoSettingsDir = "/var/opt/couchpotato";
    private $couchpotatoSettingsFile = "/var/opt/couchpotato/settings.conf";

    private function getEventMessagePath()
    {
        return "org.openmediavault.service.couchpotato";
    }

    private function getXpath()
    {
        return "/config/services/couchpotato";
    }

    private function getForksXpath()
    {
        return sprintf("%s/forks", $this->getXpath());
    }

    private function getForkXpath()
    {
        return sprintf("%s/fork", $this->getForksXpath());
    }

    public function getName()
    {
        return "Couchpotato";
    }

    public function initialize()
    {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("enumerateRepos");

        $this->registerMethod("getForks");
        $this->registerMethod("getFork");
        $this->registerMethod("setFork");
        $this->registerMethod("deleteFork");

        $this->registerMethod("doBackup");
        $this->registerMethod("doRestore");
    }

    public function getSettings($params, $context)
    {
        global $xmlConfig;

        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        $object = $xmlConfig->get($this->getXpath());

        if (is_null($object)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        $port = 5050;

        // Get current port as set in Couchpotatos config file.
        if (file_exists($this->couchpotatoSettingsFile)) {
            $config = parse_ini_file(
                $this->couchpotatoSettingsFile,
                true,
                INI_SCANNER_RAW
            );

            $coreConfigKey = "core";

            // This seems to vary between version. In 5.5 brackets
            // don't seem necessary.
            if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION == 4) {
                $coreConfigKey = "[core]";
            }

            if (!empty($config[$coreConfigKey]["port"])) {
                $port = $config[$coreConfigKey]["port"];
            }
        }

        // The port value is a readonly value fetched from the
        // CouchPotato configuration.
        $object["port"] = intval($port);

        $object["enable"] = boolval($object["enable"]);
        $object["show_tab"] = boolval($object["show_tab"]);


        // Remove useless fields from the result object.
        unset($object["forks"]);

        return $object;
    }

    public function setSettings($params, $context)
    {
        global $xmlConfig;

        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "enable": { "type": "boolean" },
                    "repo": { "type": "string" },
                    "branch": { "type": "string" },
                    "show_tab": { "type": "boolean" }
                }
            }'
        );

        // Get existing configuration object
        $oldObject = $xmlConfig->get($this->getXpath());

        if (is_null($oldObject)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        $object = array(
            "enable" => array_boolval($params, "enable"),
            "branch" => $params["branch"],
            "repo" => $params["repo"],
            "show_tab" => array_boolval($params, "show_tab"),
            "forks" => $oldObject["forks"],
        );

        if ($xmlConfig->replace($this->getXpath(), $object) === false) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(
            OMV_NOTIFY_MODIFY,
            $this->getEventMessagePath(),
            $object
        );

        return $object;
    }

    public function enumerateRepos($params, $context)
    {
        global $xmlConfig;

        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Get configuration data.
        $userRepos = $xmlConfig->getList($this->getForkXpath());

        if (is_null($userRepos)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getForkXpath()
            );
        }

        // Add default repos and their branches. Do it here so users can't
        // delete them.
        $defaultRepos = array(
            array(
                "uuid" => "2587ceb0-9b25-11e4-bd06-0800200c9a66",
                "name" => "Main repository",
                "fork" => "https://github.com/RuudBurger/CouchPotatoServer.git",
                "branches" => array(
                    "3d",
                    "desktop",
                    "develop",
                    "feature/redesign",
                    "master",
                    "tv",
                    "tv_old"
                )
            ),
        );

        $repos = array_merge($defaultRepos, $userRepos);

        return $repos;
    }

    public function getForks($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "start": { "type": "integer" },
                    "limit": { ' . $GLOBALS["OMV_JSONSCHEMA_COUNTFIELD"] . ' },
                    "sortfield": { ' . $GLOBALS["OMV_JSONSCHEMA_SORTFIELD"] . ' },
                    "sortdir": { ' . $GLOBALS["OMV_JSONSCHEMA_SORTDIR"] . ' }
                }
            }'
        );

        // Get configuration data.
        $objects = $xmlConfig->getList($this->getForkXpath());

        if (is_null($objects)) {
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $this->getForkXpath());
        }

        // Filter the result.
        return $this->applyFilter(
            $objects,
            $params["start"],
            $params["limit"],
            $params["sortfield"],
            $params["sortdir"]
        );
    }

    public function getFork($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "uuid": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        // Get the configuration object.
        $xpath = sprintf("%s[uuid='%s']", $this->getForkXpath(), $params["uuid"]);
        $object = $xmlConfig->get($xpath);

        if (is_null($object)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $xpath
            );
        }

        return $object;
    }

    public function setFork($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "uuid": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID_UNDEFINED"] . ' },
                    "name": { "type": "string" },
                    "fork": { "type": "string" }
                }
            }'
        );

        // Check if valid fork. A valid fork should start with
        // either http(s):// or git://
        if (substr($params["fork"], 0, 4) !== "http" &&
            substr($params["fork"], 0, 3) !== "git"
        ) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "Not valid git repo"
            );
        }

        // Query repo for a list of branches.
        $cmd = "git ls-remote -h "
             . $params["fork"]
             . " | sed 's?.*refs/heads/??'";

        $this->exec($cmd, $output);

        if (empty($output)) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "No branches found, check url."
            );
        }

        if (substr($output, 0, 5) === "fatal") {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "No branches found, is it a valid git repo?"
            );
        }

        // Prepare configuration data and add all branches found.
        $object = array(
            "uuid" => $params["uuid"] == $GLOBALS["OMV_UUID_UNDEFINED"]
                ? OMVUtil::uuid() : $params["uuid"],
            "name" => $params["name"],
            "fork" => $params["fork"],
            "branches" => $output,
        );

        // Set the configuration data.
        $success = false;

        if ($params["uuid"] == $GLOBALS["OMV_UUID_UNDEFINED"]) {
            // Append object to configuration.
            $success = $xmlConfig->set(
                $this->getForksXpath(),
                array("fork" => $object)
            );
        } else {
            // Update existing configuration object.
            $xpath = sprintf(
                "%s[uuid='%s']",
                $this->getForkXpath(),
                $params["uuid"]
            );
        }

        if ($success === false) {
            throw new OMVException(OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED);
        }

        return $object;
    }

    public function deleteFork($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "uuid": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        // Delete the configuration object.
        $xpath = sprintf("%s[uuid='%s']", $this->getForkXpath(), $params["uuid"]);

        return $this->deleteConfigObjectByPath($xpath);
    }

    public function doBackup($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "sharedfolderref": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        // Couchpotato needs to have run once to generate its
        // settings. Check if this is true.
        if (!file_exists($this->couchpotatoSettingsFile)) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "No settings directory found."
            );
        }

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Create a background process.
        $bgStatusFilename = $this->createBgProcStatus();
        $pid = $this->fork();

        // Parent process.
        if ($pid > 0) {
            $this->initializeBgProcStatus($bgStatusFilename, $pid);
            return $bgStatusFilename;
        }

        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            $backupPath = sprintf(
                "%s%scouchpotato.tar.gz",
                $this->getSharedFolderPathByUuid($params["sharedfolderref"]),
                DIRECTORY_SEPARATOR
            );

            $cmd = sprintf(
                "tar -zcvPf %s %s 2>&1",
                $backupPath,
                $this->couchpotatoSettingsDir
            );

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            exit(0);
        } catch (Exception $e) {
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            exit(1);
        }
    }

    public function doRestore($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "sharedfolderref": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        $backupPath = sprintf(
            "%s%scouchpotato.tar.gz",
            $this->getSharedFolderPathByUuid($params["sharedfolderref"]),
            DIRECTORY_SEPARATOR
        );

        if (is_null($backupPath)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $xpath
            );
        }

        // Check if backup is present.
        if (file_exists($backupPath)) {
            // Create a background process.
            $bgStatusFilename = $this->createBgProcStatus();
            $pid = $this->fork();

            // Parent process.
            if ($pid > 0) {
                $this->initializeBgProcStatus($bgStatusFilename, $pid);
                return $bgStatusFilename;
            }

            // Delete current settings folder.
            rmdir($this->couchpotatoSettingsDir);

            // Get the Couchpotato module.
            $moduleMgr = &OMVModuleMgr::getInstance();
            $couchpotatoModule = $moduleMgr->getModule($this->getName());

            // Child process.
            try {
                $couchpotatoModule->stopService();
                $bgOutputFilename = $this->createBgProcOutput();
                $this->updateBgProcStatus(
                    $bgStatusFilename,
                    "outputfilename",
                    $bgOutputFilename
                );

                $cmd = sprintf("tar -zxvf %s -C / 2>&1", $backupPath);

                $this->exec($cmd, $output, $bgOutputFilename);
                $this->finalizeBgProcStatus($bgStatusFilename, $output);
                $couchpotatoModule->startService();
                exit(0);
            } catch (Exception $e) {
                $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
                $couchpotatoModule->startService();
                exit(1);
            }
        } else {
            throw new OMVException(OMVErrorMsg::E_EXEC_MISC, "No backup found!");
        }
    }

    private function getSharedFolderPathByUuid($uuid)
    {
        global $xmlConfig;

        $xpath = sprintf("//system/shares/sharedfolder[uuid='%s']", $uuid);
        $sharedFolder = $xmlConfig->get($xpath);
        $sharedFolderRelDirPath = $sharedFolder["reldirpath"];

        $xpath = sprintf(
            "//system/fstab/mntent[uuid='%s']",
            $sharedFolder["mntentref"]
        );
        $mountPoint = $xmlConfig->get($xpath);
        $mountPointDir = $mountPoint["dir"];

        return $mountPointDir . DIRECTORY_SEPARATOR . $sharedFolderRelDirPath;
    }
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceCouchpotato());
