/**
 * Copyright (C) 2013-2014 OpenMediaVault Plugin Developers
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

Ext.define("OMV.module.admin.service.couchpotato.Settings", {
    extend   : "OMV.workspace.form.Panel",
    requires : [
        "OMV.data.Model",
        "OMV.data.Store"
    ],

    initComponent : function () {
        var me = this;

        me.on("load", function () {
            var checked = me.findField("enable").checked;
            var showtab = me.findField("showtab").checked;
            var parent = me.up("tabpanel");

            if (!parent)
                return;

            var managementPanel = parent.down("panel[title=" + _("Web Interface") + "]");

            if (managementPanel) {
                checked ? managementPanel.enable() : managementPanel.disable();
                showtab ? managementPanel.tab.show() : managementPanel.tab.hide();
            }
        });

        me.callParent(arguments);
    },

    rpcService   : "Couchpotato",
    rpcGetMethod : "getSettings",
    rpcSetMethod : "setSettings",

    getFormItems : function() {
        var me = this;

        return [{
            xtype    : "fieldset",
            title    : "General settings",
            defaults : {
                labelSeparator : ""
            },
            items : [{
                xtype      : "checkbox",
                name       : "enable",
                fieldLabel : _("Enable"),
                checked    : false
            },{
                xtype      : "checkbox",
                name       : "showtab",
                fieldLabel : _("Show Tab"),
                boxLabel   : _("Show tab containing Couchpotato web interface frame."),
                checked    : false
            },{
                xtype      : "combo",
                name       : "cp_repo",
                fieldLabel : "Repository",
                allowBlank : false,
                editable   : false,
                queryMode  : "local",
                store      : Ext.create("OMV.data.Store", {
                    autoLoad : true,
                    model    : OMV.data.Model.createImplicit({
                        idProperty : "name",
                        fields     : [{
                            name : "url",
                            type : "string"
                        },{
                            name : "name",
                            type : "string"
                        },{
                            name : "branches",
                            type : "array"
                        }],
                        proxy : {
                            type    : "rpc",
                            rpcData : {
                                service : "Couchpotato",
                                method  : "enumerateRepos"
                            },
                            appendSortParams : false
                        }
                    })
                }),
                displayField  : "name",
                valueField    : "url",
                triggerAction : "all",
                selectOnFocus : true,
                plugins       : [{
                    ptype : "fieldinfo",
                    text  : _("The repository you want to use. If changing from a current repository, setting will be wiped.")
                }],
                listeners : {
                    select : function(combo, records) {
                        var record = records.pop();
                        me.updateBranchCombo(record.get("branches"));
                    },
                    change : function(combo, value) {
                        var record = combo.store.findRecord("url", value);
                        me.updateBranchCombo(record.get("branches"));
                    }
                }
            },{
                xtype         : "combo",
                name          : "cp_branch",
                fieldLabel    : _("Branch"),
                queryMode     : "local",
                store         : [],
                allowBlank    : false,
                editable      : false,
                triggerAction : "all",
                plugins       : [{
                    ptype : "fieldinfo",
                    text  : _("The branch you want to use.")
                }]
            },{
                xtype   : "button",
                name    : "opencouchpotato",
                text    : _("Couchpotato Web Interface"),
                scope   : this,
                handler : function() {
                    var link = "http://" + location.hostname + ":5050/";
                    window.open(link, "_blank");
                },
                margin : "0 0 5 0"
            }]
        }];
    },

    updateBranchCombo : function(values) {
        var me = this;
        var branchCombo = me.findField("cp_branch");

        branchCombo.store.removeAll();

        for (var i = 0; i < values.length; i++) {
            // TODO: Look over use of field1
            branchCombo.store.add({ field1: values[i] });
        }
    }
});

OMV.WorkspaceManager.registerPanel({
    id        : "settings",
    path      : "/service/couchpotato",
    text      : _("Settings"),
    position  : 10,
    className : "OMV.module.admin.service.couchpotato.Settings"
});
