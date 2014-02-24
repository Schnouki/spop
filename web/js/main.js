/*
 * Copyright (C) 2014 Thomas Jost
 * This file is part of spop and is Licensed under the GPLv3. See COPYING for
 * details.
 */

requirejs.config({
    baseUrl: "js",
    paths: {
        backbone: "../lib/backbone",
        "backbone.epoxy": "../lib/backbone.epoxy",
        jquery: "../lib/jquery",
        underscore: "../lib/underscore",
        domReady: "../lib/require-domReady",
        text: "../lib/require-text"
    },
    shim: {
        backbone: {
            deps: ["underscore", "jquery"],
            exports: "Backbone"
        },
        "backbone.epoxy": ["backbone"],
        jquery: {
            exports: "$"
        },
        underscore: {
            exports: "_"
        }
    }
});

// Start the main app logic
requirejs(["backbone", "router", "domReady!"],
function(   Backbone, Router) {
    "use strict";

    var router = new Router();
    Backbone.history.start();
});
