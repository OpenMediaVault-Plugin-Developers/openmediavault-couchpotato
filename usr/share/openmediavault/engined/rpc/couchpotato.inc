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

use DateTime;
use FilesystemIterator;
use Phar;
use PharData;
use RecursiveDirectoryIterator;
use RecursiveIteratorIterator;

class OMVRpcServiceCouchpotato extends OMVRpcServiceAbstract
{
    private $couchpotatoSettingsDir = "/var/opt/couchpotato";
    private $couchpotatoSettingsFile = "/var/opt/couchpotato/settings.conf";
    private $couchpotato2SettingsDir = "/var/opt/couchpotato2";
    private $couchpotato2SettingsFile = "/var/opt/couchpotato2/settings.conf";
    private $couchpotatoUser = "couchpotato";

    private function getPluginName()
    {
        return strtolower($this->getName());
    }

    private function getEventMessagePath()
    {
        return sprintf("org.openmediavault.service.%s", $this->getPluginName());
    }

    private function getXpath()
    {
        return sprintf("/config/services/%s", $this->getPluginName());
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

        $this->registerMethod("downloadBackup");
        $this->registerMethod("uploadBackup");
        $this->registerMethod("downloadBackup2");
        $this->registerMethod("uploadBackup2");
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
        $object["enable"]         = boolval($object["enable"]);
        $object["show_tab"]       = boolval($object["show_tab"]);
        $object["newinstance"]    = boolval($object["newinstance"]);
        $object["newinstenable"]  = boolval($object["newinstenable"]);
        $object["ssl"]            = boolval($object["ssl"]);
        $object["ppass"]          = boolval($object["ppass"]);
        $object["port"]           = intval($port);


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
                    "enable"        : { "type" : "boolean" },
                    "show_tab"      : { "type" : "boolean" },
                    "newinstance"   : { "type" : "boolean" },
                    "newinstenable" : { "type" : "boolean" },
                    "ppass"         : { "type" : "boolean" },
                    "ssl"           : { "type" : "boolean" },
                    "branch"        : { "type" : "string" },
                    "repo"          : { "type" : "string" },
                    "branch2"       : { "type" : "string", "optional" : true },
                    "repo2"         : { "type" : "string", "optional" : true },
                    "username"      : { "type" : "string" },
                    "usersgrp"      : { "type" : "boolean" },
                    "umask"         : { "type" : "string" },
                    "port"          : { "type" : "integer" }
                }
            }'
        );

        // Get existing configuration object.
        $oldObject = $xmlConfig->get($this->getXpath());

        if (is_null($oldObject)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        // Run checks to verify sellected settings
        if ($params["ssl"] == "1") {
            if ($params["ppass"] == "1") {
                throw new OMVException(
                    OMVErrorMsg::E_MISC_FAILURE,
                    "You can not enable both SSL and Proxy Pass."
                );
            }
            $xSsl = $xmlConfig->get("/config/webadmin/enablessl");
            if ($xSsl == "1") {
                throw new OMVException(
                    OMVErrorMsg::E_MISC_FAILURE,
                    "You have SSL enabled for the main UI, Do NOT enable it here as well."
                );
            }
            $ySsl = $xmlConfig->get("/config/system/certificates/sslcertificate/uuid");
            if ($ySsl == "") {
                throw new OMVException(
                    OMVErrorMsg::E_MISC_FAILURE,
                    "You have not generated any SSL certificates."
                );
            }
        }

        if ($params["showtab"] == 1 && $params["ppass"] != 1) {
            if (! file_exists('/var/www/openmediavault/js/omv/module/admin/service/media/Media.js')) {
                throw new OMVException(
                    OMVErrorMsg::E_MISC_FAILURE,
                    "Install links and add an entry or enable Proxy Pass to use the web tab."
                );
            }
        }

        $object = array(
            "enable"        => array_boolval($params, "enable"),
            "show_tab"      => array_boolval($params, "show_tab"),
            "newinstance"   => array_boolval($params, "newinstance"),
            "newinstenable" => array_boolval($params, "newinstenable"),
            "ppass"         => array_boolval($params, "ppass"),
            "ssl"           => array_boolval($params, "ssl"),
            "branch"        => $params["branch"],
            "repo"          => $params["repo"],
            "branch2"       => $params["branch2"],
            "repo2"         => $params["repo2"],
            "usersgrp"      => boolval($params["usersgrp"]),
            "username"      => $params["username"],
            "umask"         => $params["umask"],
            "port"          => $params["port"],
            "forks"         => $oldObject['forks'],
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
                "type"       : "object",
                "properties" : {
                    "start"     : { "type" : "integer" },
                    "limit"     : { ' . $GLOBALS["OMV_JSONSCHEMA_COUNTFIELD"] . ' },
                    "sortfield" : { ' . $GLOBALS["OMV_JSONSCHEMA_SORTFIELD"] . ' },
                    "sortdir"   : { ' . $GLOBALS["OMV_JSONSCHEMA_SORTDIR"] . ' }
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
                "type"       : "object",
                "properties" : {
                    "uuid" : { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
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
                "type"       : "object",
                "properties" : {
                    "uuid" : { ' . $GLOBALS["OMV_JSONSCHEMA_UUID_UNDEFINED"] . ' },
                    "name" : { "type" : "string" },
                    "fork" : { "type" : "string" }
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
            "uuid"        => ($params["uuid"] == $GLOBALS["OMV_UUID_UNDEFINED"])
                ? OMVUtil::uuid() : $params["uuid"],
            "name"        => $params["name"],
            "fork"        => $params["fork"],
            "branches"    => $output,
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
                "type"       : "object",
                "properties" : {
                    "uuid" : { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        // Delete the configuration object.
        $xpath = sprintf("%s[uuid='%s']", $this->getForkXpath(), $params["uuid"]);

        return $this->deleteConfigObjectByPath($xpath);
    }

    public function downloadBackup($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        if (!file_exists("$this->couchpotatoSettingsFile")) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "Nothing to backup found."
            );
        }

        $tmpFile = sprintf("%s/%s.tar", sys_get_temp_dir(), uniqid());
        $archive = new PharData($tmpFile);

        $archive->addFile($this->couchpotatoSettingsFile, "settings.conf");

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                "$this->couchpotatoSettingsDir/database",
                FilesystemIterator::SKIP_DOTS
            )
        );

        $archive->buildFromIterator(
            $directoryIterator,
            $this->couchpotatoSettingsDir
        );

        $archive->compress(Phar::GZ);

        // Remove uncompressed tar and set $tmpFile to the compressed.
        unlink($tmpFile);
        $tmpFile = "$tmpFile.gz";

        $now = new DateTime();
        $time = $now->format(DateTime::ISO8601);

        return array(
            "contenttype" => "application/x-tar",
            "filename"    => "couchpotato-$time.tar.gz",
            "filepath"    => $tmpFile,
        );
    }

    public function uploadBackup($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
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

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "filename": { "type": "string" },
                    "filepath": { "type": "string" }
                }
            }'
        );

        // Rename file so that it's recognized by the PharData class.
        $filepath = sprintf("%s.tar.gz", $params["filepath"]);
        rename($params["filepath"], $filepath);

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                $this->couchpotatoSettingsDir,
                FilesystemIterator::SKIP_DOTS
            ),
            RecursiveIteratorIterator::CHILD_FIRST
        );

        foreach ($directoryIterator as $file) {
            if ($file->isFile()) {
                unlink($file->getPathname());
            }

            if ($file->isDir()) {
                rmdir($file->getPathname());
            }
        }

        $archive = new PharData($filepath);

        $archive->extractTo($this->couchpotatoSettingsDir);
        // Use users ownership settings
        foreach ($directoryIterator as $file) {
            chown($file->getPathname(), $object["username"]);
            if ($object["usersgrp"] = "1") {
                chgrp($file->getPathname(), "users");
            } else {
                chgrp($file->getPathname(), $object["username"]);
            }
        }

        // Old settings 
        //foreach ($directoryIterator as $file) {
        //    chown($file->getPathname(), $this->couchpotatoUser);
        //    chgrp($file->getPathname(), $this->couchpotatoUser);
        //}

        // Rename file back to orignal name so that OMV will remove it.
        rename($filepath, $params["filepath"]);

        // Send a modify signal so that the user can trigger a restart.
        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(OMV_NOTIFY_MODIFY, $this->getEventMessagePath());
    }

    public function downloadBackup2($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        if (!file_exists("$this->couchpotato2SettingsFile")) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "Nothing to backup found."
            );
        }

        $tmpFile = sprintf("%s/%s.tar", sys_get_temp_dir(), uniqid());
        $archive = new PharData($tmpFile);

        $archive->addFile($this->couchpotato2SettingsFile, "settings.conf");

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                "$this->couchpotato2SettingsDir/database",
                FilesystemIterator::SKIP_DOTS
            )
        );

        $archive->buildFromIterator(
            $directoryIterator,
            $this->couchpotatoSettingsDir
        );

        $archive->compress(Phar::GZ);

        // Remove uncompressed tar and set $tmpFile to the compressed.
        unlink($tmpFile);
        $tmpFile = "$tmpFile.gz";

        $now = new DateTime();
        $time = $now->format(DateTime::ISO8601);

        return array(
            "contenttype" => "application/x-tar",
            "filename"    => "couchpotato-$time.tar.gz",
            "filepath"    => $tmpFile,
        );
    }

    public function uploadBackup2($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
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

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "filename": { "type": "string" },
                    "filepath": { "type": "string" }
                }
            }'
        );

        // Rename file so that it's recognized by the PharData class.
        $filepath = sprintf("%s.tar.gz", $params["filepath"]);
        rename($params["filepath"], $filepath);

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                $this->couchpotato2SettingsDir,
                FilesystemIterator::SKIP_DOTS
            ),
            RecursiveIteratorIterator::CHILD_FIRST
        );

        foreach ($directoryIterator as $file) {
            if ($file->isFile()) {
                unlink($file->getPathname());
            }

            if ($file->isDir()) {
                rmdir($file->getPathname());
            }
        }

        $archive = new PharData($filepath);

        $archive->extractTo($this->couchpotato2SettingsDir);
        // Use users ownership settings
        foreach ($directoryIterator as $file) {
            chown($file->getPathname(), $object["username"]);
            if ($object["usersgrp"] = "1") {
                chgrp($file->getPathname(), "users");
            } else {
                chgrp($file->getPathname(), $object["username"]);
            }
        }

        // Rename file back to orignal name so that OMV will remove it.
        rename($filepath, $params["filepath"]);

        // Send a modify signal so that the user can trigger a restart.
        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(OMV_NOTIFY_MODIFY, $this->getEventMessagePath());
    }
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceCouchpotato());

