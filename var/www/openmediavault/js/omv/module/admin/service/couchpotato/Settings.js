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

// require("js/omv/WorkspaceManager.js")
// require("js/omv/workspace/form/Panel.js")
// require("js/omv/data/Store.js")
// require("js/omv/data/Model.js")
// require("js/omv/form/plugin/LinkedFields.js")
// require("js/omv/form/field/UserComboBox.js")

Ext.define("OMV.module.admin.service.couchpotato.Settings", {
    extend: "OMV.workspace.form.Panel",
    requires: [
        "OMV.data.Model",
        "OMV.data.Store",
        "OMV.form.field.UserComboBox"
    ],

    rpcService: "Couchpotato",
    rpcGetMethod: "getSettings",
    rpcSetMethod: "setSettings",

    initComponent: function() {
        this.on("load", function() {
            var checked = this.findField("enable").checked;
            var showtab = this.findField("show_tab").checked;
            var parent = this.up("tabpanel");

            if (!parent) {
                return;
            }

            var managementPanel = parent.down("panel[title=" + _("Web Interface") + "]");

            if (managementPanel) {
                checked ? managementPanel.enable() : managementPanel.disable();
                showtab ? managementPanel.tab.show() : managementPanel.tab.hide();
            }
        }, this);

        this.callParent(arguments);
    },

    getButtonItems: function() {
        var items = this.callParent(arguments);

        items.push({
            id: this.getId() + "-show",
            xtype: "button",
            text: _("Show"),
            icon: "images/search.png",
            iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
            scope: this,
            handler: function() {
                var port = this.getForm().findField("port").getValue();
                var link = "http://" + location.hostname + ":" + port + "/";

                window.open(link, "_blank");
            }
        }, {
            id: this.getId() + "-backup",
            xtype: "button",
            text: _("Backup"),
            icon: "images/wrench.png",
            iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
            scope: this,
            handler: Ext.Function.bind(this.onBackupButton, this)
        }, {
            id: this.getId() + "-restore",
            xtype: "button",
            text: _("Restore"),
            icon: "images/wrench.png",
            iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
            scope: this,
            handler: Ext.Function.bind(this.onRestoreButton, this),
        });

        return items;
    },

    getFormItems: function() {
        return [{
            xtype: "fieldset",
            title: "General settings",
            defaults: {
                labelSeparator: ""
            },
            items: [{
                xtype: "checkbox",
                name: "enable",
                fieldLabel: _("Enable"),
                checked: false
            }, {
                // The port value is a readonly value fetched from the
                // CouchPotato configuration.
                xtype: "hiddenfield",
                name: "port",
                submitValue: false,
                value: 5050
            }, {
                xtype: "combo",
                name: "repo",
                fieldLabel: _("Repository"),
                store: Ext.create("OMV.data.Store", {
                    autoLoad: true,
                    model: OMV.data.Model.createImplicit({
                        idProperty: "name",
                        fields: [{
                            name: "uuid",
                            type: "string"
                        }, {
                            name: "name",
                            type: "string"
                        }, {
                            name: "fork",
                            type: "string"
                        }, {
                            name: "branches",
                            type: "array"
                        }],
                        proxy: {
                            type: "rpc",
                            rpcData: {
                                service: "Couchpotato",
                                method: "enumerateRepos"
                            },
                            appendSortParams: false
                        }
                    })
                }),
                allowBlank: false,
                displayField: "fork",
                editable: false,
                listeners: {
                    scope: this,
                    change: function(combo, value) {
                        var record = combo.store.findRecord("fork", value);

                        this.updateBranchCombo(record.get("branches"));
                    },
                    select: function(combo, records) {
                        var record = records.pop();

                        this.updateBranchCombo(record.get("branches"));
                    }
                },
                queryMode: "local",
                selectOnFocus: true,
                triggerAction: "all",
                valueField: "fork",
                plugins: [{
                    ptype: "fieldinfo",
                    text: _("The repository you want to use. If changing from a current repository, setting will be wiped.")
                }]
            }, {
                xtype: "combo",
                name: "branch",
                fieldLabel: _("Branch"),
                allowBlank: false,
                editable: false,
                queryMode: "local",
                store: [],
                triggerAction: "all",
                plugins: [{
                    ptype: "fieldinfo",
                    text: _("The branch you want to use. choose master if you don't know what's involved.")
                }]
            }, {
                xtype: "checkbox",
                name: "show_tab",
                fieldLabel: _("Show Tab"),
                boxLabel: _("Show tab containing Couchpotato web interface frame."),
                checked: false
            }]
                },{
            xtype    : "fieldset",
            title    : "Custom user settings",
            defaults : {
                labelSeparator : ""
            },
            items : [{
                xtype      : "usercombo",
                name       : "username",
                fieldLabel : _("Run as User"),
                value      : "couchpotato"
            }, {
                xtype      : "checkbox",
                name       : "usersgrp",
                fieldLabel : _("Users group"),
                boxLabel   : _("Will run CP under the users group. Not recommended."),
                checked    : false
            }, {
                xtype: "numberfield",
                name: "umask",
                fieldLabel: _("Umask"),
                allowDecimals: false,
                allowNegative: false,
                allowBlank: true,
                plugins: [{
                    ptype: "fieldinfo",
                    text: _("Sets transmission's file mode creation mask.")
                }]
            }]
        }];
    },

    updateBranchCombo: function(values) {
        var branchCombo = this.findField("branch");

        branchCombo.store.removeAll();

        for (var i = 0; i < values.length; i++) {
            // TODO: Look over use of field1
            branchCombo.store.add({
                field1: values[i]
            });
        }
    },

    onBackupButton: function() {
        OMV.Download.request("Couchpotato", "downloadBackup");
    },

    onRestoreButton: function() {
        Ext.create("OMV.window.Upload", {
            title: _("Upload backup"),
            service: "Couchpotato",
            method: "uploadBackup",
            listeners: {
                scope: this,
                success: function(wnd, response) {
                    OMV.MessageBox.info(_("Restored backup"), _("Backup was successfully restored."));
                }
            }
        }).show();
    }
});

OMV.WorkspaceManager.registerPanel({
    id: "settings",
    path: "/service/couchpotato",
    text: _("Settings"),
    position: 10,
    className: "OMV.module.admin.service.couchpotato.Settings"
});

