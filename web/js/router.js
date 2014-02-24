/*
 * Copyright (C) 2014 Thomas Jost
 * This file is part of spop and is Licensed under the GPLv3. See COPYING for
 * details.
 */

define(["backbone", "jquery", "models/status"],
function(Backbone,   $,        Status) {
    "use strict";

    var Router = Backbone.Router.extend({
        routes: {
            "": "status"
        },
        models: {},

        initialize: function() {
            this.models.status = new Status();
        },

        status: function() {
            var model = this.models.status;
            require(["views/status"],
            function(StatusView) {
                var view = new StatusView({model: model});
                view.render();
            });
        }
    });

    return Router;
});
