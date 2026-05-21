// tests/test_model.cpp
#include <iostream>
#include <cassert>
#include "war_room_model.h"

using namespace warroom;

void testCreateNodes() {
    WarRoomModel model;

    // 创建分组
    WarNode group = WarNode::makeGroup("主攻方向", 100, 200);
    group.explicit_color = "#ff0000";
    Uuid group_id = model.addNode(std::move(group), model.getDocumentRootId());

    // 创建叶节点
    WarNode leaf1 = WarNode::makeLeaf("数据库优化", 150, 250);
    leaf1.tags = { "进行中" };
    leaf1.priority = 5;
    Uuid leaf1_id = model.addNode(std::move(leaf1), group_id);

    WarNode leaf2 = WarNode::makeLeaf("缓存策略", 150, 350);
    Uuid leaf2_id = model.addNode(std::move(leaf2), group_id);

    // 验证树结构
    auto top = model.getTopLevelNodes();
    assert(top.size() == 1);
    assert(top[0] == group_id);

    auto children = model.getChildren(group_id);
    assert(children.size() == 2);

    // 验证颜色继承
    Color c1 = model.getEffectiveColor(leaf1_id);
    Color c2 = model.getEffectiveColor(leaf2_id);
    assert(c1 == "#ff0000");  // 继承自 group
    assert(c2 == "#ff0000");

    // leaf2 显式覆盖颜色
    model.getNodeMutable(leaf2_id)->explicit_color = "#0000ff";
    assert(model.getEffectiveColor(leaf2_id) == "#0000ff");

    std::cout << "[PASS] testCreateNodes\n";
}

void testLinks() {
    WarRoomModel model;

    WarNode n1 = WarNode::makeLeaf("节点A", 0, 0);
    WarNode n2 = WarNode::makeLeaf("节点B", 100, 100);
    Uuid id1 = model.addNode(std::move(n1), model.getDocumentRootId());
    Uuid id2 = model.addNode(std::move(n2), model.getDocumentRootId());

    // 节点到节点连线
    WarLink link1 = WarLink::makeNodeToNode(id1, id2, LinkType::Dependency);
    link1.label = "依赖于";
    Uuid link1_id = model.addLink(std::move(link1));

    // 节点到自由点连线（进攻路线）
    WarLink link2 = WarLink::makeNodeToFree(id1, 500, 300);
    Uuid link2_id = model.addLink(std::move(link2));

    // 验证连线查询
    auto links_for_n1 = model.getLinksForNode(id1);
    assert(links_for_n1.size() == 2);

    // 验证锚点解析
    const WarLink* l1 = model.getLink(link1_id);
    Point2D start = l1->start_anchor->resolvePosition(model);
    Point2D end = l1->end_anchor->resolvePosition(model);
    assert(start.x == 0 && start.y == 0);
    assert(end.x == 100 && end.y == 100);

    std::cout << "[PASS] testLinks\n";
}

void testDeleteNodeCascades() {
    WarRoomModel model;

    WarNode group = WarNode::makeGroup("分组", 0, 0);
    Uuid group_id = model.addNode(std::move(group), model.getDocumentRootId());

    WarNode leaf = WarNode::makeLeaf("子节点", 50, 50);
    Uuid leaf_id = model.addNode(std::move(leaf), group_id);

    // 创建指向 leaf 的连线
    WarNode other = WarNode::makeLeaf("其他节点", 200, 200);
    Uuid other_id = model.addNode(std::move(other), model.getDocumentRootId());
    model.addLink(WarLink::makeNodeToNode(other_id, leaf_id));

    // 删除 group → 级联删除 leaf 和其连线
    model.removeNode(group_id);

    assert(model.getNode(group_id) == nullptr);
    assert(model.getNode(leaf_id) == nullptr);
    assert(model.getLinksForNode(leaf_id).empty());

    std::cout << "[PASS] testDeleteNodeCascades\n";
}

void testZone() {
    WarRoomModel model;

    WarNode n1 = WarNode::makeLeaf("节点1", 10, 10);
    Uuid id1 = model.addNode(std::move(n1), model.getDocumentRootId());

    WarZone zone;
    zone.name = "测试战区";
    zone.boundary = { 0, 0, 500, 500 };
    Uuid zone_id = model.addZone(std::move(zone));

    model.addNodeToZone(id1, zone_id);

    const WarZone* z = model.getZone(zone_id);
    assert(z != nullptr);
    assert(z->member_ids.size() == 1);
    assert(z->member_ids[0] == id1);

    std::cout << "[PASS] testZone\n";
}

void testViewportCulling() {
    // 模拟视口裁剪逻辑（不依赖Qt）
    WarRoomModel model;

    // 创建散布的节点
    for (int i = 0; i < 10; ++i) {
        WarNode node = WarNode::makeLeaf("Node_" + std::to_string(i),
            float(i * 200), float(i * 150));
        model.addNode(std::move(node), model.getDocumentRootId());
    }

    // 视口
    Rect viewport = { 300, 200, 400, 300 };

    // 统计可见节点
    int visible = 0;
    for (const Uuid& id : model.getTopLevelNodes()) {
        const WarNode* node = model.getNode(id);
        Rect bbox = { node->pos_x, node->pos_y, 100, 60 }; // 假设固定包围盒
        if (viewport.intersects(bbox)) {
            visible++;
        }
    }

    // 应该有 2-3 个节点在视口内
    assert(visible > 0 && visible < 10);
    std::cout << "[PASS] testViewportCulling (visible: " << visible << "/10)\n";
}

int main_test() {
    testCreateNodes();
    testLinks();
    testDeleteNodeCascades();
    testZone();
    testViewportCulling();

    std::cout << "\n所有测试通过。\n";
    return 0;
}