# Copyright (c) 2001-2015, Canal TP and/or its affiliates. All rights reserved.
#
# This file is part of Navitia,
#     the software to build cool stuff with public transport.
#
# Hope you'll enjoy and contribute to this project,
#     powered by Canal TP (www.canaltp.fr).
# Help us simplify mobility and open public transport:
#     a non ending quest to the responsive locomotion way of traveling!
#
# LICENCE: This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# Stay tuned using
# twitter @navitia
# IRC #navitia on freenode
# https://groups.google.com/d/forum/navitia
# www.navitia.io
from __future__ import absolute_import, print_function, unicode_literals, division
import logging
from datetime import timedelta
from .tests_mechanism import AbstractTestFixture
from .tests_mechanism import dataset
from .check_utils import *
from nose.tools import eq_
import jormungandr.scenarios.new_default
from jormungandr.scenarios.qualifier import min_from_criteria


def check_journeys(resp):
    assert not resp.get('journeys') or sum([1 for j in resp['journeys'] if j['type'] == "best"]) == 1


@dataset({"main_routing_test": {}})
class TestJourneysNewDefault(AbstractTestFixture):
    """
    Test the new default scenario
    All the tests are defined in "TestJourneys" class, we only change the scenario


    NOTE: for the moment we cannot import all routing tests, so we only get 2, but we need to add some more
    """

    def setup(self):
        logging.debug('setup for new default')
        from jormungandr import i_manager
        dest_instance = i_manager.instances['main_routing_test']
        self.old_scenario = dest_instance._scenario
        dest_instance._scenario = jormungandr.scenarios.new_default.Scenario()

    def teardown(self):
        from jormungandr import i_manager
        i_manager.instances['main_routing_test']._scenario = self.old_scenario

    """
    the new default scenario override the way the prev/next link are created
    """
    @staticmethod
    def check_next_datetime_link(dt, response, clockwise):
        if not response.get('journeys'):
            return
        """default next behaviour is 1s after the best or the soonest"""
        j_to_compare = min_from_criteria(generate_pt_journeys(response),
                                         new_default_pagination_journey_comparator(clockwise=clockwise))

        j_departure = get_valid_datetime(j_to_compare['departure_date_time'])
        eq_(j_departure + timedelta(seconds=1), dt)

    @staticmethod
    def check_previous_datetime_link(dt, response, clockwise):
        # All journeys in file with clockwise=true
        if not response.get('journeys'):
            return
        """default previous behaviour is 1s before the best or the latest """
        j_to_compare = min_from_criteria(generate_pt_journeys(response),
                                         new_default_pagination_journey_comparator(clockwise=clockwise))

        j_departure = get_valid_datetime(j_to_compare['arrival_date_time'])
        eq_(j_departure - timedelta(seconds=1), dt)

    def test_journeys(self):
        #NOTE: we query /v1/coverage/main_routing_test/journeys and not directly /v1/journeys
        #not to use the jormungandr database
        response = self.query_region(journey_basic_query)

        check_journeys(response)
        self.is_valid_journey_response(response, journey_basic_query)

    def test_error_on_journeys(self):
        """ if we got an error with kraken, an error should be returned"""

        query_out_of_production_bound = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}"\
            .format(from_coord=s_coord,
                    to_coord=r_coord,
                    datetime="20110614T080000")  # 2011 should not be in the production period

        response, status = self.query_no_assert("v1/coverage/main_routing_test/" + query_out_of_production_bound)

        assert status != 200, "the response should not be valid"

        check_journeys(response)
        assert response['error']['id'] == "date_out_of_bounds"
        assert response['error']['message'] == "date is not in data production period"

        #and no journey is to be provided
        assert 'journeys' not in response or len(response['journeys']) == 0

    def test_min_nb_journeys(self):
        """Checks if min_nb_journeys works.

        _night_bus_filter_base_factor is used because we need to find
        2 journeys, and we can only take the bus the day after.
        datetime is modified because, as the bus begins at 8, we need
        to check that we don't do the next on the direct path starting
        datetime.
        """
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "min_nb_journeys=3&_night_bus_filter_base_factor=86400&_override_scenario=new_default"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T075500")
        response = self.query_region(query)
        check_journeys(response)
        self.is_valid_journey_response(response, query)
        assert len(response["journeys"]) >= 3

        links = get_links_dict(response)
        prev = query_from_str(links.get("prev").get("href"))

        assert prev.get("datetime") == "20120614T080221"
        assert prev.get("datetime_represents") == "arrival"

        next = query_from_str(links.get("next").get("href"))
        assert next.get("datetime") == "20120614T080044"
        assert next.get("datetime_represents") == "departure"

    def test_datetime_represents_arrival(self):
        """
        Checks journeys when datetime == start date of production datetime.
        """
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "min_nb_journeys=3&_night_bus_filter_base_factor=86400&_override_scenario=new_default&"\
                "datetime_represents=arrival"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T185500")
        response = self.query_region(query)
        check_journeys(response)
        self.is_valid_journey_response(response, query)
        assert len(response["journeys"]) >= 3

        links = get_links_dict(response)
        prev = query_from_str(links.get("prev").get("href"))

        assert prev.get("datetime") == "20120614T180221"
        assert prev.get("datetime_represents") == "arrival"

        next = query_from_str(links.get("next").get("href"))
        assert next.get("datetime") == "20120614T180044"
        assert next.get("datetime_represents") == "departure"

    def test_first_bss_last_bss_section_mode(self):
        query = "journeys?from=0.0000898312;0.0000898312&to=0.00188646;0.00071865&datetime=20120614T075500&"\
                "_override_scenario=new_default&first_section_mode[]={first}&" \
                "last_section_mode[]={last}"
        response = self.query_region(query.format(first='bss', last='bss'))
        check_journeys(response)
        assert len(response["journeys"]) == 2
        assert response["journeys"][0]["type"] == "best"
        assert "bss" in response["journeys"][0]["tags"]

    def test_first_walking_last_walking_section_mode(self):
        query = "journeys?from=0.0000898312;0.0000898312&to=0.00188646;0.00071865&datetime=20120614T075500&"\
                "_override_scenario=new_default&first_section_mode[]={first}&" \
                "last_section_mode[]={last}"
        response = self.query_region(query.format(first='walking', last='walking'))
        check_journeys(response)
        assert len(response["journeys"]) == 2
        assert response["journeys"][0]["type"] == "best"
        assert "walking" in response["journeys"][0]["tags"]

    def test_first_bike_last_walking_section_mode(self):
        query = "journeys?from=0.0000898312;0.0000898312&to=0.00188646;0.00071865&datetime=20120614T075500&"\
                "_override_scenario=new_default&first_section_mode[]={first}&" \
                "last_section_mode[]={last}&_min_bike=50"
        response = self.query_region(query.format(first='bike', last='walking'))
        check_journeys(response)
        assert len(response["journeys"]) == 1
        assert response["journeys"][0]["type"] == "best"
        assert "bike" in response["journeys"][0]["tags"]

    def test_first_car_last_walking_section_mode(self):
        query = "journeys?from=0.0000898312;0.0000898312&to=0.00188646;0.00071865&datetime=20120614T075500&"\
                "_override_scenario=new_default&first_section_mode[]={first}&" \
                "last_section_mode[]={last}&_min_car=10"
        response = self.query_region(query.format(first='car', last='walking'))
        check_journeys(response)
        assert len(response["journeys"]) == 1
        assert response["journeys"][0]["type"] == "best"
        assert "car" in response["journeys"][0]["tags"]

    def test_first_bike_last_bss_section_mode(self):
        query = "journeys?from=0.0000898312;0.0000898312&to=0.00188646;0.00071865&datetime=20120614T075500&"\
                "_override_scenario=new_default&first_section_mode[]={first}&" \
                "last_section_mode[]={last}&_min_bike=50"
        response = self.query_region(query.format(first='bike', last='bss'))
        check_journeys(response)
        assert len(response["journeys"]) == 1
        assert response["journeys"][0]["type"] == "best"
        assert "bike" in response["journeys"][0]["tags"]

    def test_first_car_last_bss_section_mode(self):
        query = "journeys?from=0.0000898312;0.0000898312&to=0.00188646;0.00071865&datetime=20120614T075500&"\
                "_override_scenario=new_default&first_section_mode[]={first}&" \
                "last_section_mode[]={last}&_min_car=10"
        response = self.query_region(query.format(first='car', last='bss'))
        check_journeys(response)
        assert len(response["journeys"]) == 1
        assert response["journeys"][0]["type"] == "best"
        assert "car" in response["journeys"][0]["tags"]

    def test_ecologic_tag(self):
        """test that the tag ecologic is present when the journey doesn't
        produce too much CO2 compared to the car journey.
        """
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "first_section_mode[]=car&first_section_mode[]=walking&last_section_mode[]=walking&_override_scenario=new_default"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T075500")
        response = self.query_region(query)
        check_journeys(response)
        assert len(response["journeys"]) == 2
        for j in response["journeys"]:
            assert "ecologic" in j["tags"]

    def test_ecologic_tag_with_car(self):
        """test that the tag ecologic is present when the journey doesn't
        produce too much CO2 compared to the car journey.
        """
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "first_section_mode[]=car&first_section_mode[]=walking&"\
                "last_section_mode[]=walking&_min_car=0&_override_scenario=new_default"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T075500")
        response = self.query_region(query)
        check_journeys(response)
        assert len(response["journeys"]) == 3
        for j in response["journeys"]:
            if j["type"] == "best":
                assert "ecologic" not in j["tags"]
            else:
                assert "ecologic" in j["tags"]

    def test_context_car_co2_emission_with_car(self):
        """
        Test if car_co2_emission in context is the value in car journey
        """
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "first_section_mode[]=car&first_section_mode[]=walking&"\
                "last_section_mode[]=walking&_min_car=0&_override_scenario=new_default"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T075500")
        response = self.query_region(query)
        check_journeys(response)
        assert 'context' in response
        assert 'car_direct_path' in response['context']
        assert 'co2_emission' in response['context']['car_direct_path']
        assert len(response["journeys"]) == 3

        assert response['context']['car_direct_path']['co2_emission']['value'] == \
               response['journeys'][0]['co2_emission']['value'] # car co2 emission

        assert response['context']['car_direct_path']['co2_emission']['unit'] == \
               response['journeys'][0]['co2_emission']['unit']

    def test_context_car_co2_emission_without_car(self):
        """
        Test if car_co2_emission in context is the value in car journey
        """
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "&first_section_mode[]=walking&last_section_mode[]=walking&_override_scenario=new_default"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T075500")
        response = self.query_region(query)
        check_journeys(response)
        assert len(response["journeys"]) == 2

        assert 'context' in response
        assert 'car_direct_path' in response['context']
        assert 'co2_emission' in response['context']['car_direct_path']

        assert response['context']['car_direct_path']['co2_emission']['value'] == 52.5908892104
        assert response['context']['car_direct_path']['co2_emission']['unit'] == 'gEC'
        for j in response["journeys"]:
            assert "ecologic" in j["tags"]

    def test_mode_tag(self):
        query = "journeys?from={from_coord}&to={to_coord}&datetime={datetime}&"\
                "first_section_mode[]=car&first_section_mode[]=walking&"\
                "first_section_mode[]=bike&first_section_mode[]=bss&"\
                "last_section_mode[]=walking&last_section_mode[]=bss&"\
                "&_min_car=0&_override_scenario=new_default"\
                .format(from_coord=s_coord, to_coord=r_coord, datetime="20120614T075500")
        response = self.query_region(query)
        check_journeys(response)
        assert len(response["journeys"]) == 4
        assert 'car' in response["journeys"][0]["tags"]
        assert 'bss' in response["journeys"][1]["tags"]
        assert not 'walking' in response["journeys"][1]["tags"]
        assert not 'bike' in response["journeys"][1]["tags"]
        assert 'walking' in response["journeys"][2]["tags"]
        assert 'walking' in response["journeys"][3]["tags"]

@dataset({"main_ptref_test": {}})
class TestJourneysNewDefaultWithPtref(AbstractTestFixture):
    """Test the new default scenario with ptref_test data"""

    def setup(self):
        logging.debug('setup for new default')
        from jormungandr import i_manager
        dest_instance = i_manager.instances['main_ptref_test']
        self.old_scenario = dest_instance._scenario
        dest_instance._scenario = jormungandr.scenarios.new_default.Scenario()

    def teardown(self):
        from jormungandr import i_manager
        i_manager.instances['main_ptref_test']._scenario = self.old_scenario

    def test_strange_line_name(self):
        response = self.query("v1/coverage/main_ptref_test/journeys"
                              "?from=stop_area:stop2&to=stop_area:stop1"
                              "&datetime=20140107T100000")
        check_journeys(response)
        eq_(len(response['journeys']), 1)
