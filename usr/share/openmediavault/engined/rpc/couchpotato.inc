<?php

/**
 * Copyright (C) 2010-2012 Ian Moore <imooreyahoo@gmail.com>
 * Copyright (C) 2013-2015 OpenMediaVault Plugin Developers.
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
 //

namespace OMV\Engined\Rpc;

use DateTime;
use FilesystemIterator;
use Exception;
use OMV\Config\Database;
use OMV\Engine\Notify\Dispatcher;
use OMV\Rpc\ServiceAbstract;
use OMV\Rpc\ServiceManager as RpcServiceManager;
use OMV\System\Process;
use SplFileObject;
use Phar;
use PharData;
use RecursiveDirectoryIterator;
use RecursiveIteratorIterator;

class Couchpotato extends ServiceAbstract
{
    private $database;

    private $dataModelPath = 'conf.service.couchpotato';

    private $forkDataModelPath = 'conf.service.couchpotato.fork';

    private $settings1DataModelPath = 'conf.service.couchpotato.setsettings1';

    private $settings2DataModelPath = 'conf.service.couchpotato.setsettings2';

    private $dispatcher;

    private $eventMessagePath = 'org.openmediavault.conf.services.couchpotato';

    public function __construct(Database $database = null, Dispatcher $dispatcher = null)
    {
        $this->database = $database ?: Database::getInstance();
        $this->dispatcher = $dispatcher ?: Dispatcher::getInstance();
    }

    public function getName()
    {
        return 'Couchpotato';
    }

    public function initialize()
    {

        $this->registerMethod("getInstance");
        $this->registerMethod("setInstance");

        $this->registerMethod("getSettings1");
        $this->registerMethod("setSettings1");
        $this->registerMethod("getSettings2");
        $this->registerMethod("setSettings2");

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

    public function getInstance($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $object = $this->database->get($this->dataModelPath);

        $object->remove('instances');
        $object->remove('forks');

        return $object->getAssoc();
    }

    public function setInstance($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, 'rpc.couchpotato.setinstance');

        $object = $this->database->get($this->dataModelPath);
        $object->setAssoc($params);

        $this->database->set($object);
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath, $object);

        return $object->getAssoc();
    }

    public function getSettings1($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $object = $this->database->get($this->settings1DataModelPath);

        $port = 5050;

        // Get current port as set in Couchpotatos config file.
        if (file_exists($object->get('db-folder')."/settings.conf")) {
            $config = parse_ini_file(
                $object->get('db-folder')."/settings.conf",
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

        $object->add('port', 'integer');
        $object->set('port', intval($port));

        return $object->getAssoc();
    }

    public function setSettings1($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        if (empty($params['mntentref']) || $params['mntentref'] == "Select a volume or leave blank for default.") {
            $this->validateMethodParams($params, 'rpc.couchpotato.setsettings1');
            $params['db-folder'] = "/var/opt/couchpotato";
        } else {
            $this->validateMethodParams($params, 'rpc.couchpotato.setsettings1');
            $db = \OMV\Config\Database::getInstance();
            $deviceDir = $db->getAssoc("conf.system.filesystem.mountpoint", $params['mntentref']);
            $params['db-folder'] = sprintf("{$deviceDir['dir']}/%s", $this->getName());
        }

        // Get existing configuration object
        $oldObject = $this->database->get($this->settings1DataModelPath);

        // Run checks to verify sellected settings
        if ($params['ssl'] == 1) {
            if ($params['ppass'] == 1) {
                throw new Exception('You can not enable both SSL and Proxy Pass.');
            }
            $db = \OMV\Config\Database::getInstance();
            $getInfo = $db->getAssoc("conf.webadmin", $params['enablessl']);
            $xSsl = $getInfo['enablessl'];
            if ($xSsl == "1") {
                throw new Exception('You have SSL enabled for the main UI, Do NOT enable it here as well.');
            }

            $getInfo = $db->getAssoc('conf.system.certificate.ssl');

            if (! array_key_exists('uuid', $getInfo[0])) {
                throw new Exception('You have not generated any SSL certificates.');
            }
        }

        if ($params['showtab'] == 1 && $params['ppass'] != 1) {
            if (! file_exists('/var/www/openmediavault/js/omv/module/admin/service/media/Media.js')) {
                throw new Exception('Install links and add an entry or enable Proxy Pass to use the web tab.');
            }
        }

        if ($params['db-folder'] != $oldObject->get('db-folder') && $oldObject->get('db-folder') != '') {
            $cmd = 'mv '.$oldObject->get('db-folder').'/ '.$params['db-folder'].'/';
            //throw new Exception('You.' . $cmd);
            exec($cmd, $output);
        }

        $oldObject->setAssoc($params);

        $this->database->set($oldObject);
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath, $oldObject);

        return $oldObject->getAssoc();
    }

    public function getSettings2($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $object = $this->database->get($this->settings2DataModelPath);

        $port = 5051;

        // Get current port as set in Couchpotatos config file.
        if (file_exists($object->get('db-folder')."/settings.conf")) {
            $config = parse_ini_file(
                $object->get('db-folder')."/settings.conf",
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

        $object->add('port', 'integer');
        $object->set('port', intval($port));

        return $object->getAssoc();
    }

    public function setSettings2($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        if (empty($params['mntentref']) || $params['mntentref'] == "Select a volume or leave blank for default.") {
            $this->validateMethodParams($params, 'rpc.couchpotato.setsettings2');
            $params['db-folder'] = "/var/opt/couchpotato2";
        } else {
            $this->validateMethodParams($params, 'rpc.couchpotato.setsettings2');
            $db = \OMV\Config\Database::getInstance();
            $deviceDir = $db->getAssoc("conf.system.filesystem.mountpoint", $params['mntentref']);
            $params['db-folder'] = sprintf("{$deviceDir['dir']}/%s", $this->getName());
        }

        // Get existing configuration object
        $oldObject = $this->database->get($this->settings2DataModelPath);

        // Run checks to verify sellected settings
        if ($params['ssl'] == 1) {
            if ($params['ppass'] == 1) {
                throw new Exception('You can not enable both SSL and Proxy Pass.');
            }
            $db = \OMV\Config\Database::getInstance();
            $getInfo = $db->getAssoc("conf.webadmin", $params['enablessl']);
            $xSsl = $getInfo['enablessl'];
            if ($xSsl == "1") {
                throw new Exception('You have SSL enabled for the main UI, Do NOT enable it here as well.');
            }

            $getInfo = $db->getAssoc('conf.system.certificate.ssl');

            if (! array_key_exists('uuid', $getInfo[0])) {
                throw new Exception('You have not generated any SSL certificates.');
            }
        }

        if ($params['showtab'] == 1 && $params['ppass'] != 1) {
            if (! file_exists('/var/www/openmediavault/js/omv/module/admin/service/media/Media.js')) {
                throw new Exception('Install links and add an entry or enable Proxy Pass to use the web tab.');
            }
        }

        if ($params['db-folder'] != $oldObject->get('db-folder') && $oldObject->get('db-folder') != '') {
            $cmd = 'mv '.$oldObject->get('db-folder').'/ '.$params['db-folder'].'/';
            //throw new Exception('You.' . $cmd);
            exec($cmd, $output);
        }

        $oldObject->setAssoc($params);

        $this->database->set($oldObject);
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath, $oldObject);

        return $oldObject->getAssoc();
    }

    public function enumerateRepos($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Get the configuration object.
        $db = \OMV\Config\Database::getInstance();

        $userRepos = $db->getAssoc($this->forkDataModelPath);

        if (is_null($userRepos)) {
            throw new Exception('No directory for downloads set, please set one before enabling the service');
        }

        // Add default repos and their branches. Do it here so users can't delete them.
        $defaultRepos = [
            [
                "uuid" => "2587ceb0-9b25-11e4-bd06-0800200c9a66",
                "name" => "Main repository",
                "fork" => "https://github.com/RuudBurger/CouchPotatoServer.git",
                "branches" => [
                    "3d",
                    "desktop",
                    "develop",
                    "feature/redesign",
                    "master",
                    "tv",
                    "tv_old"
                ]
            ]
        ];

        $repos = array_merge($userRepos, $defaultRepos);

        return $repos;
    }

    public function getForks($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Validate the parameters of the RPC service method.
        //$this->validateMethodParams($params, "rpc.common.getlist");

        // Get the configuration object.
        $db = \OMV\Config\Database::getInstance();

        $userRepos = $db->getAssoc($this->forkDataModelPath);

        return $this->applyFilter($userRepos, $params['start'], $params['limit'],
            $params['sortfield'], $params['sortdir']
        );
    }

    public function getFork($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Get the configuration object.
        $db = \OMV\Config\Database::getInstance();

        return $db->getAssoc($this->forkDataModelPath, $params['uuid']);
    }

    public function setFork($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, 'rpc.sickbeard.setfork');

        // Check if valid fork. A valid fork should start with either http(s):// or git://
        if (substr($params["fork"], 0, 4) !== "http" &&
            substr($params["fork"], 0, 3) !== "git"
        ) {
            throw new Exception('Not valid git repo.');
        }

        // Query repo for a list of branches.
        $cmd = "git ls-remote -h "
             . $params["fork"]
             . " | sed 's?.*refs/heads/??'";

        $this->exec($cmd, $output);

        if (empty($output)) {
            throw new Exception('No branches found, check url.');
        }

        if (substr($output, 0, 5) === "fatal") {
            throw new Exception('No branches found, is it a valid git repo?');
        }

        if (count($output) == "1") {
            $output[1] = "";
        }

        $object = new \OMV\Config\ConfigObject($this->forkDataModelPath);

        $params['branches'] = $output;

        $object->setAssoc($params);

        $this->database->set($object);

        return $object->getAssoc();
    }

    public function deleteFork($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Delete the configuration object.
        $db = \OMV\Config\Database::getInstance();
        $object = $db->get($this->forkDataModelPath, $params['uuid']);
        $db->delete($object);

        // Return the deleted configuration object.
        return $object->getAssoc();
    }

    public function downloadBackup($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $db = \OMV\Config\Database::getInstance();

        $settings = $db->getAssoc($this->settings1DataModelPath);

        if (!file_exists($settings['db-folder'] . "/settings.conf")) {
            throw new Exception('Nothing to backup found.');
        }

        $tmpFile = sprintf('%s/%s.tar', sys_get_temp_dir(), uniqid());
        $archive = new PharData($tmpFile);

        $archive->addFile($settings['db-folder'], 'settings.conf');

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                sprintf('%s/database', $settings['db-folder']),
                FilesystemIterator::SKIP_DOTS
            )
        );

        $archive->buildFromIterator($directoryIterator, $settings['db-folder']);
        $archive->compress(Phar::GZ);

        // Remove uncompressed tar and set $tmpFile to the compressed.
        unlink($tmpFile);
        $tmpFile = sprintf('%s.gz', $tmpFile);

        $now = new DateTime();
        $time = $now->format(DateTime::ISO8601);

        return [
            'contenttype' => 'application/x-tar',
            'filename' => sprintf('couchpotato-%s.tar.gz', $time),
            'filepath' => $tmpFile,
        ];
    }

    public function uploadBackup($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, 'rpc.plugin.upload');

        $db = \OMV\Config\Database::getInstance();

        $settings = $db->getAssoc($this->settings1DataModelPath);

        // Rename file so that it's recognized by the PharData class.
        $filepath = sprintf('%s.tar.gz', $params['filepath']);
        rename($params['filepath'], $filepath);

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                $settings['db-folder'],
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
        $archive->extractTo($settings['db-folder']);

        foreach ($directoryIterator as $file) {
            chown($file->getPathname(), $settings['user']);
            chgrp($file->getPathname(), $settings['user']);
        }

        // Rename file back to orignal name so that OMV will remove it.
        rename($filepath, $params['filepath']);

        // Send a modify signal so that the user can trigger a restart.
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath);
    }

    public function downloadBackup2($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $db = \OMV\Config\Database::getInstance();

        $settings = $db->getAssoc($this->settings2DataModelPath);

        if (!file_exists($settings['db-folder'] . "/settings.conf")) {
            throw new Exception('Nothing to backup found.');
        }

        $tmpFile = sprintf('%s/%s.tar', sys_get_temp_dir(), uniqid());
        $archive = new PharData($tmpFile);

        $archive->addFile($settings['db-folder'], 'settings.conf');

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                sprintf('%s/database', $settings['db-folder']),
                FilesystemIterator::SKIP_DOTS
            )
        );

        $archive->buildFromIterator($directoryIterator, $settings['db-folder']);
        $archive->compress(Phar::GZ);

        // Remove uncompressed tar and set $tmpFile to the compressed.
        unlink($tmpFile);
        $tmpFile = sprintf('%s.gz', $tmpFile);

        $now = new DateTime();
        $time = $now->format(DateTime::ISO8601);

        return [
            'contenttype' => 'application/x-tar',
            'filename' => sprintf('couchpotato-%s.tar.gz', $time),
            'filepath' => $tmpFile,
        ];
    }

    public function uploadBackup2($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, 'rpc.plugin.upload');

        $db = \OMV\Config\Database::getInstance();

        $settings = $db->getAssoc($this->settings2DataModelPath);

        // Rename file so that it's recognized by the PharData class.
        $filepath = sprintf('%s.tar.gz', $params['filepath']);
        rename($params['filepath'], $filepath);

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                $settings['db-folder'],
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
        $archive->extractTo($settings['db-folder']);

        foreach ($directoryIterator as $file) {
            chown($file->getPathname(), $settings['user']);
            chgrp($file->getPathname(), $settings['user']);
        }

        // Rename file back to orignal name so that OMV will remove it.
        rename($filepath, $params['filepath']);

        // Send a modify signal so that the user can trigger a restart.
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath);
    }
}
