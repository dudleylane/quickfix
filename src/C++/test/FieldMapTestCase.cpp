/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#include "config.h"

#include <FieldMap.h>
#include <Group.h>
#include <Message.h>
#include <malloc.h>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("FieldMapTests")
{
    SECTION("setMessageOrder")
    {
        int order[] = {1, 2, 3, 0}; // '0' is used to signify the end of array passed to FieldMap()
        FieldMap fieldMap(order);
        fieldMap.setField(3, "account");
        fieldMap.setField(1, "adv_id");
        fieldMap.setField(2, "adv_ref_id");

        int pos1 = 0, pos2 = 0, pos3 = 0;
        int iterationCount = 0;
        for (FieldMap::iterator itr = fieldMap.begin(); itr != fieldMap.end(); itr++, iterationCount++)
        {
            if (iterationCount == 0)
            {
                pos1 = itr->getTag();
            }
            else if (iterationCount == 1)
            {
                pos2 = itr->getTag();
            }
            else if (iterationCount == 2)
            {
                pos3 = itr->getTag();
            }
        }

        CHECK(1 == pos1);
        CHECK(2 == pos2);
        CHECK(3 == pos3);
    }

    SECTION("addGroupPtr_nullptr")
    {
        FieldMap fieldMap;
        fieldMap.addGroupPtr(1, nullptr);
        CHECK(0U == fieldMap.groupCount(0));
    }

    SECTION("removeGroup_allGroupsWithSameTag")
    {
        FieldMap fieldMap;
        FieldMap group1;
        group1.setField(2, "field2");

        FieldMap group2;
        group2.setField(2, "field2");

        fieldMap.addGroup(1, group1);
        fieldMap.addGroup(1, group2);
        CHECK(2ul == fieldMap.groupCount(1));

        fieldMap.removeGroup(2, 1);
        fieldMap.removeGroup(1, 1);
        CHECK(0ul == fieldMap.groupCount(1));
    }

    SECTION("removeGroup_whenCountFieldIsRemoved")
    {
        FieldMap fieldMap;
        FieldMap group1;
        group1.setField(2, "field2");

        FieldMap group2;
        group2.setField(2, "field2");

        fieldMap.addGroup(1, group1);
        fieldMap.addGroup(1, group2);
        CHECK(2ul == fieldMap.groupCount(1));

        fieldMap.removeField(1);
        CHECK(0ul == fieldMap.groupCount(1));
    }

    SECTION("hasGroup_groupExists")
    {
        FieldMap fieldMap;
        FieldMap group;
        fieldMap.addGroup(1, group);

        CHECK(fieldMap.hasGroup(1));
    }

    SECTION("hasGroup_groupDoesNotExist")
    {
        FieldMap fieldMap;
        FieldMap group;
        fieldMap.addGroup(1, group);

        CHECK(!fieldMap.hasGroup(2));
    }

    SECTION("totalFields")
    {
        FieldMap fieldMap;
        fieldMap.setField(1, "field1");
        fieldMap.setField(2, "field2");
        fieldMap.setField(3, "field3");

        FieldMap group1;
        group1.setField(4, "field4");
        fieldMap.addGroup(10, group1);
        FieldMap group2;
        group2.setField(5, "field5");
        group2.setField(6, "field6");
        fieldMap.addGroup(20, group2);

        CHECK(8ul == fieldMap.totalFields());
    }

    SECTION("setField_16FieldsAlreadyExist_fieldSet")
    {
        FieldMap fieldMap;
        fieldMap.setField(1, "field1");
        fieldMap.setField(2, "field2");
        fieldMap.setField(3, "field3");
        fieldMap.setField(4, "field4");
        fieldMap.setField(5, "field5");
        fieldMap.setField(6, "field6");

        fieldMap.setField(7, "field7");
        fieldMap.setField(8, "field8");
        fieldMap.setField(9, "field9");
        fieldMap.setField(10, "field10");
        fieldMap.setField(11, "field11");
        fieldMap.setField(12, "field12");
        fieldMap.setField(13, "field13");
        fieldMap.setField(14, "field14");
        fieldMap.setField(15, "field15");
        fieldMap.setField(16, "field16");
        fieldMap.setField(17, "field17");
        fieldMap.setField(18, "field18");

        FieldBase expectedTag18(18, "field18_new");

        fieldMap.setField(expectedTag18);

        FieldBase actualTag18(18, "");
        fieldMap.getFieldIfSet(actualTag18);

        CHECK(18 == actualTag18.getTag());
        CHECK("field18_new" == actualTag18.getString());
    }

    SECTION("copyAssignmentWithGroups")
    {
        FieldMap original;
        original.setField(1, "account");

        FieldMap group;
        group.setField(11, "clordid1");
        original.addGroup(78, group);

        FieldMap group2;
        group2.setField(11, "clordid2");
        original.addGroup(78, group2);

        FieldMap copy;
        copy = original;

        CHECK(copy.getField(1) == "account");
        CHECK(copy.groupCount(78) == 2);

        FieldMap retrievedGroup;
        copy.getGroup(1, 78, retrievedGroup);
        CHECK(retrievedGroup.getField(11) == "clordid1");

        copy.setField(1, "modified");
        CHECK(original.getField(1) == "account");
    }

    SECTION("copyConstructorWithGroups")
    {
        FieldMap original;
        original.setField(1, "account");

        FieldMap group;
        group.setField(11, "clordid1");
        original.addGroup(78, group);

        FieldMap copy(original);

        CHECK(copy.getField(1) == "account");
        CHECK(copy.groupCount(78) == 1);

        copy.setField(1, "modified");
        CHECK(original.getField(1) == "account");
    }

    SECTION("addGroupPreservesGroupType")
    {
        FieldMap parent;
        Group group(268, 269, message_order(269, 0));
        group.setField(269, "0");
        group.setField(270, "100.5");

        parent.addGroup(268, group);

        FieldMap retrieved;
        parent.getGroup(1, 268, retrieved);
        CHECK(retrieved.getField(269) == "0");
        CHECK(retrieved.getField(270) == "100.5");

        // Field values survive slicing, so the checks above cannot tell a Group from a sliced
        // FieldMap. The stored object itself must still be a Group, count and delimiter intact (#67).
        const Group *stored = dynamic_cast<const Group *>(parent.groups().find(268)->second.front());
        REQUIRE(stored != nullptr);
        CHECK(stored->field() == 268);
        CHECK(stored->delim() == 269);
    }

    SECTION("copyFieldMapWithGroupsPreservesGroupType")
    {
        FieldMap parent;
        Group group(268, 269, message_order(269, 0));
        group.setField(269, "1");
        parent.addGroup(268, group);

        FieldMap copy(parent);
        CHECK(copy.groupCount(268) == 1);

        FieldMap retrieved;
        copy.getGroup(1, 268, retrieved);
        CHECK(retrieved.getField(269) == "1");

        // As above: the copy's stored group must be a Group, not a FieldMap sliced from one (#67).
        const Group *stored = dynamic_cast<const Group *>(copy.groups().find(268)->second.front());
        REQUIRE(stored != nullptr);
        CHECK(stored->field() == 268);
        CHECK(stored->delim() == 269);

        FieldMap assigned;
        assigned = parent;
        const Group *assignedGroup = dynamic_cast<const Group *>(assigned.groups().find(268)->second.front());
        REQUIRE(assignedGroup != nullptr);
        CHECK(assignedGroup->delim() == 269);
    }

    SECTION("arenaReusedAcrossClearCycles")
    {
        FieldMap parent;
        Group group(268, 269, message_order(269, 0));
        group.setField(269, "0");

        for (int cycle = 0; cycle < 3; ++cycle)
        {
            for (int i = 0; i < 10; ++i)
            {
                parent.addGroup(268, group);
            }
            CHECK(parent.groupCount(268) == 10);
            parent.clear();
            CHECK(parent.groupCount(268) == 0);
        }
    }

    SECTION("arenaOverflowFallsBackToHeap")
    {
        FieldMap parent;
        Group group(268, 269, message_order(269, 0));
        group.setField(269, "0");

        for (int i = 0; i < 40; ++i)
        {
            parent.addGroup(268, group);
        }
        CHECK(parent.groupCount(268) == 40);
        parent.clear();
        CHECK(parent.groupCount(268) == 0);
    }

    SECTION("moveAssignmentReleasesTheTargetsGroups")
    {
        // Move-assigning onto a map that already holds groups must destroy them, nested ones and
        // their field vectors included, or each assignment leaks them. Heap use is sampled after a
        // warm-up and must not grow with the number of assignments. (Under AddressSanitizer
        // mallinfo2 does not see the heap, so this passes trivially there; LeakSanitizer reports
        // the leak instead.)
        auto withGroups = []
        {
            FieldMap map;
            map.setField(1, "account");
            for (int i = 0; i < 4; ++i)
            {
                FieldMap inner;
                inner.setField(55, "SYMBOL" + std::to_string(i));
                FieldMap group;
                group.setField(11, std::string(64, 'x'));
                group.addGroup(146, inner);                // nested, through the arena
                map.addGroup(78, group);                   // through the arena
                FieldMap *heapGroup = new FieldMap(group); // owned by the map, not in the arena
                map.addGroupPtr(73, heapGroup);
            }
            return map;
        };

        FieldMap target = withGroups();
        size_t before = 0;
        for (int i = 0; i < 2000; ++i)
        {
            target = withGroups();
            if (i == 200)
            {
                before = mallinfo2().uordblks;
            }
        }
        const long growth = static_cast<long>(mallinfo2().uordblks) - static_cast<long>(before);
        CHECK(growth < 64 * 1024);
        CHECK(target.groupCount(78) == 4);
        CHECK(target.groupCount(73) == 4);

        FieldMap &self = target;
        target = std::move(self); // self-move leaves a valid map
        CHECK(target.groupCount(78) == 4);
    }
}