# Room Types

## Design Decisions

### Workshop Specialization
**Question:** Single workshop or specialist workshops (woodworking, metalworking)?

**Decision:** Multiple specialized workshops
- Will result in more expansive bases. This is preferred (similar to Going Medieval and Oxygen Not Included)
- Gives an optimization challenge of putting bedrooms and workrooms together to optimize time
- Encourages thoughtful base layout

### Conflicting Room Types
**Question:** If a room has conflicting types, does it take the type from the highest calculation (most items of that type) or no type? What would a kitchen with a dining room table and chairs be? There are many chairs, so it might take dining room over kitchen even though there is specialist equipment. This does not seem right.

**Considerations:**
- Could weight items by type influence, but that doesn't sound right
- Could allow the user to change the type, but that UI might not be discoverable
- Most games do this automatically (Rimworld, Oxygen Not Included, Going Medieval)

## Room Behaviors

### Type Assignment
- A room gets a type from the items that are put in it
- A room cannot have multiple types (for simplicity, no butcher + kitchen combo rooms)
- If there are conflicting types, it gets... *[resolution needed]*

### Ownership
- The owner of the room can be set by changing the ownership of an item in the room
- Changing the ownership of one item will change all the items in the room
- New items built or moved to the room will inherit this ownership
- Items in a room may not have mixed ownership
- There should be some visual affordance about this
- There can be multiple owners for a room/items (for things like a workshop)
- Other colonists can still enter the room, but they cannot use anything that is not owned by them. This will allow repairing and upgrades to happen

### Adjacency Bonuses
- Rooms can have adjacency bonuses
- Example: A bathroom connected to a bedroom benefits them both
- May also apply to kitchen/butcher/dining room and many others

### Material Effects
- Rooms inherit traits from the materials that they are made of
- Fine/rare/expensive materials provide a happiness bonus when using the room
- Sterile materials improve hygiene and medical effectiveness

## All Room Properties

Every room has these base properties:
- **Spaciousness** (size) - Affects comfort and effectiveness
- **Value/Impressiveness/Wealth/Quality** score - Affects colonist mood
- **Sterility** - Affects medical success and food safety
- **Type** (computed, see behaviors) - Determines room function
- **Owner** (computed, see behaviors) - Determines who can use items

## Room Types

### Workshop
*Where things get crafted*

**Note:** This is type-specific, not output-specific. For instance, "armor" can be made of wood in the wood shop, metal in the metal shop, etc. There is no "armor shop" or "gun shop".

**Variants:**
- **Wood shop** - Woodworking crafts
- **Smithy** - Forged goods
- **Metal shop / Machine shop** (pick one?) - Advanced metal fabrication
- **Electronics shop** - Electronic devices
- **Masonry shop** - Stone and brick items
- **Tailory** - Clothing and fabrics
- **Kitchen** - Food preparation
- **Butcher** - Meat processing

**Effects:**
- Grants bonus to crafting speed
- Enables advanced crafting / tool linking

### Bedroom
Colonists strongly prefer bedrooms and have a happiness reduction if not. They also prefer to have their own owned bedroom.

A bedroom has an owner, in which all items in the bedroom are used by that owner. A bedroom may have no owners, and the act of sleeping in a bed once does not set the owner.

**Variants:**
- **Bunkhouse** - Shared sleeping space (multiple beds)
- **Bedroom** - Individual sleeping quarters
- **(?)** **Guest room** - For visitors
- **(?)** **Jail cell** - For prisoners

**Effects:**
- Grants bonus to sleep speed/recovery
- Provides mood benefits based on quality

### Bathroom
If attached to a single bedroom, it inherits ownership from that bedroom. Both the bedroom and the bathroom get adjacency bonuses.

**Note:** Is this a special case on ownership?

**Effects:**
- Grants bonus to hygiene
- Required for colonist happiness

### Dining Room
A communal eating space.

**Effects:**
- Grants bonus to satiety
- Social bonding during meals
- Mood improvement from nice dining environment

### Storage
Dedicated storage areas for materials and goods.

**Effects:**
- Grants bonus to storage amount (?)
- Sterility (see materials) will grant bonus to decay rate of items that can decay
- Organization benefits for hauling tasks

### Classroom
Educational space for teaching and learning.

**Effects:**
- Grants a bonus to learning
- Can host classes (this may be difficult to implement)

### Library
Quiet study space with access to manuals and books.

**Effects:**
- Grants a bonus to self learning
- Required for writing manuals
- Mood benefit from reading

### Recreation Room
Entertainment and leisure space.

**Effects:**
- Grants a bonus to recreation (fun?)
- Reduces stress
- Social bonding

### Exercise Room
Physical fitness area.

**Effects:**
- Grants a bonus to exercise
- Improves strength/agility over time
- Health benefits

### Hospital / Clinic
Medical treatment facility.

**Effects:**
- Gains big bonus from sterility
- Grants a bonus to natural healing
- Grants a success rate bonus to medical procedures
- Critical for treating injuries and illness

### (?) Office
Administrative workspace.

**Question:** What is office work? Will we have administrative roles?
